const TOKEN_KEY = 'trinity_access_token';

export function getAuthHeaders(): HeadersInit {
  const token = window.localStorage.getItem(TOKEN_KEY);
  return token ? { Authorization: `Bearer ${token}` } : {};
}

export function getStoredAuthToken(): string | null {
  return window.localStorage.getItem(TOKEN_KEY);
}
