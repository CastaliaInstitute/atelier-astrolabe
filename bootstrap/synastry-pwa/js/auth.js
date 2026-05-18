import { getConfig, saveConfig } from "./config.js";

/** Parse tokens from castalia.institute redirect hash if present. */
export function captureAuthFromUrl() {
  const hash = window.location.hash?.slice(1);
  if (!hash) return false;
  const params = new URLSearchParams(hash);
  const access = params.get("access_token");
  const refresh = params.get("refresh_token");
  if (access) {
    saveConfig({ accessToken: access, refreshToken: refresh || "" });
    history.replaceState(null, "", window.location.pathname + window.location.search);
    return true;
  }
  return false;
}

export function signInUrl() {
  const c = getConfig();
  const origin = c.castaliaWebOrigin || "https://castalia.institute";
  const redirect = encodeURIComponent(window.location.origin + window.location.pathname);
  return `${origin}/auth/mynah-device/?redirect=${redirect}`;
}

export function isSignedIn() {
  return Boolean(getConfig().accessToken);
}

export function signOut() {
  saveConfig({ accessToken: "", refreshToken: "" });
}
