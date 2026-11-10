// SPDX-License-Identifier: GPL-3.0-only
#include "dev/Backtrace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <cstdlib>
#include <exception>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace Komorebi {

#if defined(Q_OS_WIN)

namespace {

QtMessageHandler previousHandler = nullptr;
HANDLE currentProcess = nullptr;

QString symbolFor(DWORD64 address, QString* location);

/// The module a code address belongs to, and how far into it the address sits. Symbol names need a
/// program database next to the binary, which a release build does not ship, but the module and its
/// offset are always there -- that is the part that says whether the fault is ours or Qt's.
QString moduleFor(DWORD64 address, DWORD64* offset)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(static_cast<quintptr>(address)), &module)
        || !module)
        return {};

    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0)
        return {};

    *offset = address - reinterpret_cast<quintptr>(module);
    return QString::fromWCharArray(path, int(length)).section(QLatin1Char('\\'), -1);
}

/// One stack line: the raw address, the owning module and its offset, then the symbol and source
/// location when a program database happens to be available.
QString frameText(DWORD64 address)
{
    DWORD64 offset = 0;
    const QString module = moduleFor(address, &offset);

    QString location;
    const QString name = symbolFor(address, &location);

    QString text = QStringLiteral("0x%1").arg(address, 0, 16);
    if (!module.isEmpty())
        text += QStringLiteral(" %1+0x%2").arg(module, QString::number(offset, 16));
    if (!name.isEmpty())
        text += QLatin1Char(' ') + name;
    if (!location.isEmpty())
        text += QLatin1Char(' ') + location;
    return text;
}

QString symbolFor(DWORD64 address, QString* location)
{
    DWORD lineOffset = 0;
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    if (SymGetLineFromAddr64(currentProcess, address, &lineOffset, &line))
        *location = QStringLiteral("%1:%2").arg(QString::fromLocal8Bit(line.FileName)).arg(line.LineNumber);

    char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 offset = 0;
    if (SymFromAddr(currentProcess, address, &offset, symbol))
        return QString::fromLocal8Bit(symbol->Name);
    return {};
}

/// Walks a stack from a context: this is the path used when the process dies on a fault rather than
/// on a Qt check, and the faulting thread has to be walked with its own registers.
void writeFramesFromContext(QTextStream& out, CONTEXT context)
{
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;

    for (int index = 0; index < 64; ++index) {
        const BOOL walked = StackWalk64(IMAGE_FILE_MACHINE_AMD64, currentProcess, GetCurrentThread(), &frame, &context,
                                        [](HANDLE, DWORD64 base, PVOID buffer, DWORD size, LPDWORD read) -> BOOL {
                                            SIZE_T copied = 0;
                                            const BOOL ok = ReadProcessMemory(currentProcess, reinterpret_cast<LPCVOID>(base), buffer, size, &copied);
                                            if (read)
                                                *read = static_cast<DWORD>(copied);
                                            return ok;
                                        },
                                        SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
        if (!walked || frame.AddrPC.Offset == 0)
            break;

        out << QStringLiteral("#%1 %2\n").arg(index).arg(frameText(frame.AddrPC.Offset));
    }
}

QString crashFile()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("crash.txt"));
}

void writeQtFrames(const QString& message)
{
    QFile file(crashFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;

    QTextStream out(&file);
    out << QStringLiteral("fatal: %1\n").arg(message.trimmed());

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool symbols = SymInitialize(currentProcess, nullptr, TRUE);

    void* frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT index = 0; index < count; ++index) {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[index]);
        const QString name = symbols ? frameText(address) : QStringLiteral("0x%1").arg(address, 0, 16);
        out << QStringLiteral("#%1 %2\n").arg(index).arg(name);
    }
    if (symbols)
        SymCleanup(currentProcess);
    out.flush();
}

LONG WINAPI faultFilter(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord && info->ContextRecord) {
        QFile file(crashFile());
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            QTextStream out(&file);
            out << QStringLiteral("fault: 0x%1 at 0x%2\n")
                       .arg(info->ExceptionRecord->ExceptionCode, 0, 16)
                       .arg(reinterpret_cast<quintptr>(info->ExceptionRecord->ExceptionAddress), 0, 16);
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            if (SymInitialize(currentProcess, nullptr, TRUE)) {
                writeFramesFromContext(out, *info->ContextRecord);
                SymCleanup(currentProcess);
            }
            out.flush();
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void fatalHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtFatalMsg) {
        writeQtFrames(message);
        TerminateProcess(currentProcess, 3);
    }
    if (previousHandler)
        previousHandler(type, context, message);
}

/// Writes a short record and the current stack; used by the handlers for the ways a C++ program dies
/// without raising an exception (terminate, a pure virtual call, the CRT's invalid parameter path).
void writeShortRecord(const char* what)
{
    QFile file(crashFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;

    QTextStream out(&file);
    out << QString::fromLatin1(what) << QLatin1Char('\n');

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool symbols = SymInitialize(currentProcess, nullptr, TRUE);
    void* frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT index = 0; index < count; ++index) {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[index]);
        const QString name = symbols ? frameText(address) : QStringLiteral("0x%1").arg(address, 0, 16);
        out << QStringLiteral("#%1 %2\n").arg(index).arg(name);
    }
    if (symbols)
        SymCleanup(currentProcess);
    out.flush();
}

[[noreturn]] void onTerminate()
{
    writeShortRecord("terminate called");
    TerminateProcess(currentProcess, 3);
    std::abort();
}

void onPureCall()
{
    writeShortRecord("pure virtual call");
    TerminateProcess(currentProcess, 3);
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    writeShortRecord("invalid parameter");
    TerminateProcess(currentProcess, 3);
}

}  // namespace

void installFatalBacktrace()
{
    currentProcess = GetCurrentProcess();
    previousHandler = qInstallMessageHandler(fatalHandler);
    SetUnhandledExceptionFilter(faultFilter);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
}

#else

void installFatalBacktrace() {}

#endif

}  // namespace Komorebi
