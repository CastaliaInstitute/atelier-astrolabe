# Family Synastry

Landing site for [synastry.castalia.institute](https://synastry.castalia.institute) — relational awareness for modern households, from [Castalia Institute](https://castalia.institute).

## Site

Static pages live in `docs/`. The root URL is a **PWA** that mirrors the Astrolabe **synastry clock face**: dual-wheel chart, family target cycling, brief spoken reading, and ask (voice or text). Marketing copy is at [`docs/about.html`](docs/about.html).

GitHub Actions deploys to GitHub Pages on push to `main`. For voice, set repository secrets `MYNAH_SUPABASE_URL` and `MYNAH_SUPABASE_ANON_KEY` (same as Astrolabe), or enter them in the in-app Settings.

### iOS install

Open [synastry.castalia.institute](https://synastry.castalia.institute) in Safari → Share → **Add to Home Screen**. The app runs standalone with the round chart UI.

## DNS (Cloudflare)

`synastry.castalia.institute` → `castaliainstitute.github.io` (DNS only, not proxied).

```bash
export CLOUDFLARE_API_TOKEN='...'   # Zone DNS Edit + Zone Read
./scripts/cf-dns-synastry-github-pages.sh
```

Or run the **Sync synastry DNS** workflow in GitHub Actions (uses repo/org `CLOUDFLARE_API_TOKEN` secret).

After DNS propagates, confirm the custom domain under **Settings → Pages** in this repository.
