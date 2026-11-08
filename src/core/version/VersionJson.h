// SPDX-License-Identifier: GPL-3.0-only
/*
 *  A version description, read the way the game itself describes one: a main class, a list of
 *  libraries with rules, and the arguments in either the old or the new shape.
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Komorebi {

/// What the rules of a library or an argument ask about the machine it runs on.
struct Environment {
    QString osName;  ///< windows, darwin, linux
    QString osArch;  ///< x86, x86_64, arm64
    QString osVersion;

    static Environment current();
};

/// One allowed value of a rule: matches when every field that is set matches.
struct Rule {
    enum class Action { Allow, Disallow };

    Action action = Action::Allow;
    QString osName;
    QString osArch;
    QString osVersion;
    bool hasFeatures = false;  ///< the rule is about optional features; we have none, so it never matches

    bool matches(const Environment& environment) const;
};

/// The classpath separator for a platform: ";" on Windows, ":" everywhere else.
QString classpathSeparator(const Environment& environment);

/// The file name of the Java launcher on a platform.
QString javaExecutableName(const Environment& environment);

/// A rule list decides by its last matching entry; with no matching entry the answer is allowed.
bool rulesAllow(const QList<Rule>& rules, const Environment& environment);

struct Download {
    QString path;  ///< relative to the libraries root
    QString url;
    QString sha1;
    qint64 size = 0;
    bool valid() const { return !path.isEmpty(); }
};

struct Library {
    QString name;  ///< group:artifact:version[:classifier]
    Download artifact;
    bool hasNatives = false;
    /// The native jar for the current platform, when the library ships one.
    Download natives;

    bool isNative() const { return hasNatives && natives.valid(); }
};

/// An argument is either a literal or a value that is only used when its rules allow it.
struct Argument {
    QString value;
    QList<Rule> rules;

    bool applies(const Environment& environment) const { return rulesAllow(rules, environment); }
};

struct VersionJson {
    QString id;
    QString mainClass;
    QString assets;                  ///< asset index id
    int javaMajor = 8;               ///< the Java the version asks for
    QString javaComponent;           ///< Mojang's name for that runtime, e.g. java-runtime-epsilon
    QList<Library> libraries;
    QList<Library> mavenFiles;  ///< files a loader needs beside the game, e.g. the Forge installer
    Download clientJar;              ///< the game itself
    Download assetIndex;            ///< the index of the assets, not the assets
    QList<Argument> gameArguments;   ///< new style
    QList<Argument> jvmArguments;    ///< new style
    QStringList legacyArguments;     ///< old style: one string, split on spaces

    /// Parses a version file; returns a version with an empty id when the file cannot be read.
    static VersionJson fromFile(const QString& path);
    static VersionJson fromJson(const QByteArray& json);

    /// The game arguments for this platform, in order.
    QStringList resolvedGameArguments(const Environment& environment) const;
    QStringList resolvedJvmArguments(const Environment& environment) const;
};

}  // namespace Komorebi
