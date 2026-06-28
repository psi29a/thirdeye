#!/usr/bin/env bash
# Export Coverity Scan defects for this project via the undocumented
# session-cookie endpoints used by the "View Defects" page.
# Bash port of https://github.com/IronBlood/export-coverity-defects (JS).
#
# Setup (one-time, from your browser):
#   1. Log in to scan.coverity.com and open the View Defects page for this
#      project. Note which scan{N}.scan.coverity.com host you land on.
#   2. Open devtools -> Network tab, refresh, and find the request that returns
#      the defect list (look for table.json or similar). Copy:
#        - the Cookie header value of COVJSESSIONID... (name varies by instance)
#        - the projectId and viewId from the request URL (the SPA hash route
#          /#/project-view/A/B contains both but the order isn't obvious)
#   3. Export as env vars:
#        export COVERITY_HOST=scan7.scan.coverity.com   # optional, default scan4.coverity.com
#        export COVERITY_SESSION_ID=...
#        export COVERITY_PROJECT_ID=...
#        export COVERITY_VIEW_ID=...
#      Consider stashing these in a gitignored .envrc or your password manager.
#
# Usage:
#   tools/coverity-export.sh           # raw JSON to stdout
#   tools/coverity-export.sh csv       # CSV to stdout (one row per defect)
#   tools/coverity-export.sh > defects.json
#
# WARNING: the session cookie is short-lived (~1 hour idle). If you get an HTML
# login page back instead of JSON, refresh the cookie.

set -euo pipefail

: "${COVERITY_SESSION_ID:?set COVERITY_SESSION_ID (browser cookie COVJSESSIONID-build)}"
: "${COVERITY_XSRF_TOKEN:?set COVERITY_XSRF_TOKEN (browser cookie XSRF-TOKEN)}"
: "${COVERITY_PROJECT_ID:?set COVERITY_PROJECT_ID (numeric, from /#/project-view/A/B URL)}"
: "${COVERITY_VIEW_ID:?set COVERITY_VIEW_ID (numeric, from /#/project-view/A/B URL)}"

host="${COVERITY_HOST:-scan7.scan.coverity.com}"
cookie_name="${COVERITY_COOKIE_NAME:-COVJSESSIONID-build}"
cookie="isAuthenticated=true; ${cookie_name}=${COVERITY_SESSION_ID}; XSRF-TOKEN=${COVERITY_XSRF_TOKEN}"
referer="https://${host}/"

# Modern Coverity Connect REST endpoint. Confirm via devtools Network tab if
# this 404s -- the path may differ between instances.
rows="${COVERITY_ROWS:-2500}"
url="https://${host}/api/viewContents/issues/v1/${COVERITY_VIEW_ID}?projectId=${COVERITY_PROJECT_ID}&offset=0&rowCount=${rows}"

fetch() {
    curl -fsSL \
        -H "Referer: ${referer}" \
        -H "Cookie: ${cookie}" \
        -H "X-XSRF-TOKEN: ${COVERITY_XSRF_TOKEN}" \
        -H "Accept: application/json" \
        "$url"
}

case "${1:-json}" in
    json)
        fetch
        ;;
    csv)
        # Modern endpoint returns .viewContentsV1.rows (array of {key:value}
        # objects). Fall back to the old .resultSet.results shape if needed.
        fetch | jq -r '
            (.viewContentsV1.rows // .resultSet.results // []) as $rows
            | ($rows[0] // {} | keys_unsorted) as $cols
            | $cols, ($rows[] | [.[ $cols[] ]])
            | @csv'
        ;;
    *)
        echo "usage: $0 [json|csv]" >&2
        exit 2
        ;;
esac
