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

QString describe(QString name, DWORD64 displacement)
{
    if (name.isEmpty())
        return QStringLiteral("?");
    return name + QStringLiteral("+0x") + QString::number(displacement, 16);
}

void writeBacktrace(const QString& message)
{
    QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("crash.txt")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;

    QTextStream out(&file);
    out << QStringLiteral("fatal: %1\n").arg(message.trimmed());

    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool symbols = SymInitialize(process, nullptr, TRUE);

    void* frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT index = 0; index < count; ++index) {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[index]);
        QString location;

        DWORD displacement = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        if (symbols && SymGetLineFromAddr64(process, address, &displacement, &line))
            location = QStringLiteral("%1:%2").arg(QString::fromLocal8Bit(line.FileName)).arg(line.LineNumber);

        QString name;
        if (symbols) {
            char buffer[sizeof(SYMBOL_INFO) + 256] = {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 offset = 0;
            if (SymFromAddr(process, address, &offset, symbol))
                name = describe(QString::fromLocal8Bit(symbol->Name), offset);
        }

        out << QStringLiteral("#%1 0x%2 %3 %4\n")
                   .arg(index)
                   .arg(address, 0, 16)
                   .arg(name.isEmpty() ? QStringLiteral("?") : name, location);
    }

    if (symbols)
        SymCleanup(process);
    out.flush();
}

void fatalHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtFatalMsg) {
        writeBacktrace(message);
        TerminateProcess(GetCurrentProcess(), 3);
    }
    if (previousHandler)
        previousHandler(type, context, message);
}

}  // namespace

void installFatalBacktrace()
{
    previousHandler = qInstallMessageHandler(fatalHandler);
}

#else

void installFatalBacktrace() {}

#endif

}  // namespace Komorebi
