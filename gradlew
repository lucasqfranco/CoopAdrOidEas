#!/usr/bin/env bash
set -e

APP_HOME="$( cd "${0%/*}" && pwd -P )"
WRAPPER_JAR="$APP_HOME/gradle/wrapper/gradle-wrapper.jar"

if [ -f "$WRAPPER_JAR" ]; then
    exec java -Xmx4g -Dfile.encoding=UTF-8 -classpath "$WRAPPER_JAR" org.gradle.wrapper.GradleWrapperMain "$@"
elif command -v gradle >/dev/null 2>&1; then
    exec gradle "$@"
else
    echo "ERROR: Gradle Wrapper JAR not found ($WRAPPER_JAR) and 'gradle' command not found in PATH." >&2
    echo "Please install Gradle (>= 8.11) or run 'gradle wrapper' to generate the wrapper." >&2
    exit 1
fi
