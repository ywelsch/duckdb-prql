#!/bin/bash

# Usage: ./extension-upload.sh <name> <extension_version> <duckdb_version> <architecture> <copy_to_latest>
# <name>                : Name of the extension
# <extension_version>   : Version (commit / version tag) of the extension
# <duckdb_version>      : Version (commit / version tag) of DuckDB
# <architecture>        : Architecture target of the extension binary
# <copy_to_latest>      : Set this as the latest version ("true" / "false", default: "false")

set -e

ext="build/release/extension/$1/$1.duckdb_extension"

# compress extension binary
gzip < $ext > "$1.duckdb_extension.gz"

# upload compressed extension binary
ssh ${SSH_USER}@${SSH_HOST} "mkdir -p ${UPLOAD_BASE_PATH}/$1/$2/$3/$4"
scp $1.duckdb_extension.gz ${SSH_USER}@${SSH_HOST}:${UPLOAD_BASE_PATH}/$1/$2/$3/$4/$1.duckdb_extension.gz

if [ $5 = 'true' ] ; then
  ssh ${SSH_USER}@${SSH_HOST} "mkdir -p ${UPLOAD_BASE_PATH}/$1/latest/$3/$4"
  scp $1.duckdb_extension.gz ${SSH_USER}@${SSH_HOST}:${UPLOAD_BASE_PATH}/$1/latest/$3/$4/$1.duckdb_extension.gz
fi
