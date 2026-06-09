#!/bin/bash
latestTag=$(git branch --show-current)
gitCommitHash=$(git rev-parse --short HEAD)
gitCommitDate=$(git log -1 --format=%cd --date=format:"%Y-%m-%d")

macroDefinition="#pragma once"
macroDefinition+="\n\n#define GIT_LATEST_TAG \"$latestTag\""
macroDefinition+="\n#define GIT_COMMIT_HASH \"$gitCommitHash\""
macroDefinition+="\n#define GIT_COMMIT_DATE \"$gitCommitDate\""

echo -e "$macroDefinition" > src/git_info.h
