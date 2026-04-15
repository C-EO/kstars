#!/bin/bash
# fix_qt_keywords.sh
# Replaces Qt keyword macros with their Q_-prefixed equivalents.
# All replacements are Qt4/Qt5/Qt6 compatible.
# Required when building with -DQT_NO_KEYWORDS (e.g. Debian/Ubuntu KDE packaging).
#
# Handles:
#   signals:              -> Q_SIGNALS:
#   public/private/protected slots: -> Q_SLOTS:
#   emit <expr>           -> Q_EMIT <expr>  (code lines only, not comments)
#
# Usage: bash tools/fix_qt_keywords.sh
#        Run from anywhere inside the repository tree.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "Scanning: $ROOT_DIR"
echo ""

MODIFIED=0
SKIPPED=0

while IFS= read -r -d '' file; do
    BEFORE=$(md5sum "$file" | cut -d' ' -f1)

    # signals: -> Q_SIGNALS:  (safe to replace everywhere; never appears in comments)
    sed -i 's/\bsignals:/Q_SIGNALS:/g' "$file"

    # public/private/protected slots: -> Q_SLOTS:  (same)
    sed -i 's/\bpublic slots:/public Q_SLOTS:/g' "$file"
    sed -i 's/\bprivate slots:/private Q_SLOTS:/g' "$file"
    sed -i 's/\bprotected slots:/protected Q_SLOTS:/g' "$file"

    # emit -> Q_EMIT  — skip lines that are comments (start with // or *)
    perl -i -pe 's/\bemit /Q_EMIT /g unless /^\s*[*\/]/' "$file"

    AFTER=$(md5sum "$file" | cut -d' ' -f1)

    if [ "$BEFORE" != "$AFTER" ]; then
        echo "  Fixed: ${file#$ROOT_DIR/}"
        MODIFIED=$((MODIFIED + 1))
    else
        SKIPPED=$((SKIPPED + 1))
    fi
done < <(find "$ROOT_DIR" \
    \( -name "*.h" -o -name "*.cpp" \) \
    -not -path "*/.git/*" \
    -not -path "*/build/*" \
    -not -path "*/obj-*/*" \
    -print0)

echo ""
echo "Done. Modified: $MODIFIED files, Unchanged: $SKIPPED files."
