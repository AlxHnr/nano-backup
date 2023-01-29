#!/bin/sh -e

# Runs clang-tidy on the repository, excluding ./third-party/ libraries.

cd "$(dirname "$0")/.."

wrapper="./scripts/clang-tidy-wrapper.sh"

exec run-clang-tidy -quiet -clang-tidy-binary "$wrapper" '/(src|test)/'
