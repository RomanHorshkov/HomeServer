function b64u(buf) {
  const b = new Uint8Array(buf);
  let s = ""; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

export async function ensureDeviceKey() {
  // simple: generate every time for now; we’ll persist later
  return await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign","verify"]);
}

export async function exportPublicJwk(kp) {
  return await crypto.subtle.exportKey("jwk", kp.publicKey); // {kty:"OKP", crv:"Ed25519", x:"..."}
}

export async function jwkThumbprint(jwk) {
  const canonical = JSON.stringify({ crv: jwk.crv, kty: jwk.kty, x: jwk.x });
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(canonical));
  return b64u(digest);
}
