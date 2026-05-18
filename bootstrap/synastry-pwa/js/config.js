/** Runtime config: injected at deploy (config.local.js) or Settings UI. */
const fromWindow = typeof window !== "undefined" ? window.__CASTALIA_CONFIG__ : null;
const fromStorage =
  typeof localStorage !== "undefined"
    ? {
        supabaseUrl: localStorage.getItem("castalia.supabaseUrl") || "",
        supabaseAnonKey: localStorage.getItem("castalia.supabaseAnonKey") || "",
        accessToken: localStorage.getItem("castalia.accessToken") || "",
        refreshToken: localStorage.getItem("castalia.refreshToken") || "",
      }
    : {};

export function getConfig() {
  return {
    supabaseUrl: (fromWindow?.supabaseUrl || fromStorage.supabaseUrl || "").replace(/\/$/, ""),
    supabaseAnonKey: fromWindow?.supabaseAnonKey || fromStorage.supabaseAnonKey || "",
    accessToken: fromStorage.accessToken || fromWindow?.accessToken || "",
    refreshToken: fromStorage.refreshToken || fromWindow?.refreshToken || "",
    castaliaWebOrigin: fromWindow?.castaliaWebOrigin || "https://castalia.institute",
  };
}

export function saveConfig(partial) {
  if (partial.supabaseUrl != null) {
    localStorage.setItem("castalia.supabaseUrl", partial.supabaseUrl);
  }
  if (partial.supabaseAnonKey != null) {
    localStorage.setItem("castalia.supabaseAnonKey", partial.supabaseAnonKey);
  }
  if (partial.accessToken != null) {
    localStorage.setItem("castalia.accessToken", partial.accessToken);
  }
  if (partial.refreshToken != null) {
    localStorage.setItem("castalia.refreshToken", partial.refreshToken);
  }
}

export function hasVoiceConfig() {
  const c = getConfig();
  return Boolean(c.supabaseUrl && c.supabaseAnonKey);
}
