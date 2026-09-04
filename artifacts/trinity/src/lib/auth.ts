const TOKEN_KEY = 'trinity_access_token';

function safeGetToken(): string | null {
  try {
    if (typeof window === "undefined" || !window.localStorage) return null;
    return window.localStorage.getItem(TOKEN_KEY);
  } catch {
    return null;
  }
}

export function getAuthHeaders(): HeadersInit {
  const token = safeGetToken();
  return token ? { Authorization: `Bearer ${token}` } : {};
}

export function getStoredAuthToken(): string | null {
  return safeGetToken();
}
