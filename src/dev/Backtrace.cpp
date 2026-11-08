// SPDX-License-Identifier: GPL-3.0-only
#include "dev/Backtrace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

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

QString describe(QString name, DWORD64 displacement)
{
    if (name.isEmpty())
        return QStringLiteral("?");
    return name + QStringLiteral("+0x") + QString::number(displacement, 16);
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
        return describe(QString::fromLocal8Bit(symbol->Name), offset);
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

        QString location;
        const QString name = symbolFor(frame.AddrPC.Offset, &location);
        out << QStringLiteral("#%1 0x%2 %3 %4\n")
                   .arg(index)
                   .arg(frame.AddrPC.Offset, 0, 16)
                   .arg(name.isEmpty() ? QStringLiteral("?") : name, location);
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
        QString location;
        const QString name = symbols ? symbolFor(address, &location) : QString();
        out << QStringLiteral("#%1 0x%2 %3 %4\n")
                   .arg(index)
                   .arg(address, 0, 16)
                   .arg(name.isEmpty() ? QStringLiteral("?") : name, location);
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

}  // namespace

void installFatalBacktrace()
{
    currentProcess = GetCurrentProcess();
    previousHandler = qInstallMessageHandler(fatalHandler);
    SetUnhandledExceptionFilter(faultFilter);
}

#else

void installFatalBacktrace() {}

#endif

}  // namespace Komorebi
