import { FormEvent, useMemo, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';

const TOKEN_KEY = 'trinity_access_token';

type AuthPanelProps = { onAuthChanged?: () => void };

export function AuthPanel({ onAuthChanged }: AuthPanelProps) {
  const initialToken = useMemo(() => localStorage.getItem(TOKEN_KEY), []);
  const [token, setToken] = useState(initialToken);
  const [mode, setMode] = useState<'login' | 'register'>('login');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [message, setMessage] = useState('');

  async function submit(event: FormEvent) {
    event.preventDefault();
    setMessage('');
    try {
      const response = await fetch(`/api/auth/${mode}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ email, password }),
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.detail || 'Authentication failed');
      localStorage.setItem(TOKEN_KEY, data.access_token);
      setToken(data.access_token);
      setPassword('');
      setMessage(`Signed in as ${data.user.email}`);
      onAuthChanged?.();
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Authentication failed');
    }
  }

  function logout() {
    localStorage.removeItem(TOKEN_KEY);
    setToken(null);
    setMessage('Signed out');
    onAuthChanged?.();
  }

  if (token) {
    return (
      <div className="flex items-center gap-2 rounded-lg border border-border/60 bg-card/90 px-3 py-2 shadow-lg backdrop-blur">
        <span className="text-xs text-primary">Account session active</span>
        <Button size="sm" variant="outline" onClick={logout}>Sign out</Button>
      </div>
    );
  }

  return (
    <Card className="w-72 border-border/70 bg-card/95 shadow-xl backdrop-blur">
      <CardHeader className="p-4 pb-2">
        <CardTitle className="text-sm">{mode === 'login' ? 'Sign in to Trinity' : 'Create a Trinity account'}</CardTitle>
        <CardDescription className="text-xs">Use an account to keep jobs and conversations private.</CardDescription>
      </CardHeader>
      <CardContent className="p-4 pt-2">
        <form className="space-y-2" onSubmit={submit}>
          <Input aria-label="Email" type="email" placeholder="you@example.com" value={email} onChange={(event) => setEmail(event.target.value)} required />
          <Input aria-label="Password" type="password" placeholder="At least 8 characters" value={password} onChange={(event) => setPassword(event.target.value)} minLength={8} required />
          <Button className="w-full" size="sm" type="submit">{mode === 'login' ? 'Sign in' : 'Register'}</Button>
        </form>
        {message && <p className="mt-2 text-xs text-muted-foreground">{message}</p>}
        <button className="mt-2 text-xs text-primary underline-offset-4 hover:underline" onClick={() => setMode(mode === 'login' ? 'register' : 'login')} type="button">
          {mode === 'login' ? 'Need an account?' : 'Already have an account?'}
        </button>
      </CardContent>
    </Card>
  );
}
