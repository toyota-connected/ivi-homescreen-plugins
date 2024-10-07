#!/bin/bash
CONFIG_FILE_PATH="/home/devbox/dev/workspace-automation/app/ivi-homescreen-plugins/clang-format"
find . -iname "*.h" -o -iname "*.cc" | xargs clang-format -i -style=file -fallback-style=none -assume-filename=${CONFIG_FILE_PATH}
