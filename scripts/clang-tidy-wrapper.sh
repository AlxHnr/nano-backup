#!/bin/sh -e

# Wrapper for complementing `run-clang-tidy` in ci jobs.

exec clang-tidy --warnings-as-errors='*' "$@"
