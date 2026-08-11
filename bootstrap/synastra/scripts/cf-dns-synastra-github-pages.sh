#!/usr/bin/env bash
# Create or fix synastra.castalia.institute -> GitHub Pages (DNS-only CNAME).
# Requires: curl, jq
#
# Auth (either):
#   export CLOUDFLARE_API_TOKEN='...'
#   # castalia.institute repo uses CLOUDFLARE_TOKEN in .env — that name is accepted too.
# or:
#   export CF_API_EMAIL='you@example.com'
#   export CF_API_GLOBAL_API_KEY='...'
#
# Usage:
#   ./scripts/cf-dns-synastra-github-pages.sh
#   DRY_RUN=1 ./scripts/cf-dns-synastra-github-pages.sh

set -euo pipefail

DRY_RUN="${DRY_RUN:-0}"
ZONE_NAME="${ZONE_NAME:-castalia.institute}"
RECORD_NAME="${RECORD_NAME:-synastra}"
TARGET="${GITHUB_PAGES_CNAME_TARGET:-castaliainstitute.github.io}"

if [[ -z "${CLOUDFLARE_API_TOKEN:-}" && -n "${CLOUDFLARE_TOKEN:-}" ]]; then
  CLOUDFLARE_API_TOKEN="$CLOUDFLARE_TOKEN"
fi

declare -a CURL_AUTH
if [[ -n "${CLOUDFLARE_API_TOKEN:-}" ]]; then
  CURL_AUTH=(-H "Authorization: Bearer ${CLOUDFLARE_API_TOKEN}")
elif [[ -n "${CF_API_EMAIL:-}" && -n "${CF_API_GLOBAL_API_KEY:-}" ]]; then
  CURL_AUTH=(-H "X-Auth-Email: ${CF_API_EMAIL}" -H "X-Auth-Key: ${CF_API_GLOBAL_API_KEY}")
else
  echo "error: set CLOUDFLARE_API_TOKEN (recommended), or CF_API_EMAIL + CF_API_GLOBAL_API_KEY" >&2
  echo "       API token: Zone → DNS Edit + Zone → Read, limited to zone ${ZONE_NAME}" >&2
  exit 1
fi

API="https://api.cloudflare.com/client/v4"

cf_get() {
  curl -sS "${API}$1" \
    "${CURL_AUTH[@]}" \
    -H "Content-Type: application/json"
}

cf_json() {
  local method="$1"
  local path="$2"
  local data="${3:-}"
  if [[ "$DRY_RUN" == "1" ]]; then
    echo "DRY_RUN ${method} ${path} ${data}" >&2
    return 0
  fi
  if [[ -n "$data" ]]; then
    curl -sS -X "$method" "${API}${path}" \
      "${CURL_AUTH[@]}" \
      -H "Content-Type: application/json" \
      -d "$data"
  else
    curl -sS -X "$method" "${API}${path}" \
      "${CURL_AUTH[@]}" \
      -H "Content-Type: application/json"
  fi
}

zone_resp="$(cf_get "/zones?name=${ZONE_NAME}")"
zone_id="$(echo "$zone_resp" | jq -r '.result[0].id // empty')"

if [[ -z "$zone_id" ]]; then
  echo "error: could not resolve zone id for ${ZONE_NAME}" >&2
  echo "$zone_resp" | jq . >&2
  exit 1
fi

fqdn="${RECORD_NAME}.${ZONE_NAME}"
echo "zone_id=${zone_id}  record=${fqdn}  ->  ${TARGET}  (proxied=false)"

existing="$(cf_get "/zones/${zone_id}/dns_records?name=${fqdn}&per_page=100")"

if [[ "$(echo "$existing" | jq -r '.success')" != "true" ]]; then
  echo "error: list dns_records failed" >&2
  echo "$existing" | jq . >&2
  exit 1
fi

need_create="1"
while IFS= read -r row; do
  [[ -z "$row" ]] && continue
  id="$(echo "$row" | jq -r '.id')"
  typ="$(echo "$row" | jq -r '.type')"
  content="$(echo "$row" | jq -r '.content' | sed 's/\.$//')"
  proxied="$(echo "$row" | jq -r '.proxied')"

  if [[ "$typ" == "CNAME" && "$content" == "$TARGET" && "$proxied" == "false" ]]; then
    echo "ok: CNAME already correct (id=${id}, DNS only)"
    need_create="0"
    continue
  fi

  if [[ "$typ" == "CNAME" && "$content" == "$TARGET" && "$proxied" == "true" ]]; then
    echo "patch: disabling proxy on existing CNAME (id=${id})"
    patch_body="$(jq -nc \
      --arg name "$RECORD_NAME" \
      --arg content "$TARGET" \
      '{type:"CNAME", name:$name, content:$content, ttl:1, proxied:false}')"
    if [[ "$DRY_RUN" == "1" ]]; then
      cf_json PUT "/zones/${zone_id}/dns_records/${id}" "$patch_body" || true
    else
      resp="$(cf_json PUT "/zones/${zone_id}/dns_records/${id}" "$patch_body")"
      echo "$resp" | jq .
      [[ "$(echo "$resp" | jq -r '.success')" == "true" ]] || exit 1
    fi
    need_create="0"
    continue
  fi

  echo "delete: ${typ} ${content} (id=${id})"
  if [[ "$DRY_RUN" == "1" ]]; then
    cf_json DELETE "/zones/${zone_id}/dns_records/${id}" || true
  else
    del="$(cf_json DELETE "/zones/${zone_id}/dns_records/${id}")"
    echo "$del" | jq .
    [[ "$(echo "$del" | jq -r '.success')" == "true" ]] || exit 1
  fi
done < <(echo "$existing" | jq -c '.result[]')

if [[ "$need_create" == "1" ]]; then
  echo "create: CNAME ${RECORD_NAME} -> ${TARGET}"
  body="$(jq -nc \
    --arg name "$RECORD_NAME" \
    --arg content "$TARGET" \
    '{type:"CNAME", name:$name, content:$content, ttl:1, proxied:false}')"
  if [[ "$DRY_RUN" == "1" ]]; then
    cf_json POST "/zones/${zone_id}/dns_records" "$body" || true
  else
    resp="$(cf_json POST "/zones/${zone_id}/dns_records" "$body")"
    echo "$resp" | jq .
    [[ "$(echo "$resp" | jq -r '.success')" == "true" ]] || exit 1
  fi
fi

echo
echo "Verify:"
echo "  dig ${fqdn} CNAME +short @1.1.1.1"
echo "Then GitHub: repo Settings → Pages → Custom domain DNS check"
