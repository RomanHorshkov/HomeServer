// src/api/auth.js
// TEMP MOCK for dev testing. Replace with real API later.

const MAP = {
  'admin@dev':     2, // admin
  'publisher@dev': 1, // publisher
  'viewer@dev':    0, // viewer
};

// Simulate network delay and basic credential check
export async function login(email, password) {
  await new Promise(r => setTimeout(r, 400)); // tiny delay
  const role = MAP[email];
  if (!role || password !== 'test') {
    throw new Error('Login failed');
  }
  // Return the same shape your backend will return (normalized)
  return {
    uid:    'deadbeefdeadbeefdeadbeefdeadbeef',
    role,                 // 0 viewer, 1 publisher, 2 admin
    accTok: 'mock-access',
    refTok: 'mock-refresh',
  };
}
