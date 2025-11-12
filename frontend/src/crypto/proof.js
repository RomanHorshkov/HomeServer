import { ensureDeviceKey } from "./deviceKey";

function b64uJSON(obj) {
  return btoa(unescape(encodeURIComponent(JSON.stringify(obj))))
    .replace(/\+/g,"-").replace(/\//g,"_").replace(/=+$/,"");
}

async function signCompactEd25519(privateKey, header, payload) {
  const enc = new TextEncoder();
  const h = b64uJSON(header);
  const p = b64uJSON(payload);
  const toSign = enc.encode(`${h}.${p}`);
  const sig = await crypto.subtle.sign({ name: "Ed25519" }, privateKey, toSign);
  const s = btoa(String.fromCharCode(...new Uint8Array(sig)))
              .replace(/\+/g,"-").replace(/\//g,"_").replace(/=+$/,"");
  return `${h}.${p}.${s}`;
}

// Build proof for specific request & challenge
export async function makeProof({ method, url, kid, nonce, device_id }) {
  const kp = await ensureDeviceKey(); // same key used earlier
  const u = new URL(url);
  const now = Math.floor(Date.now() / 1000);

  const header = { typ: "dpop+jwt", alg: "EdDSA", kid };
  const payload = {
    htm: (method || "GET").toUpperCase(),
    htu: `${u.origin}${u.pathname}`,
    iat: now,
    jti: crypto.randomUUID(),
    nonce,
    device_id
  };

  return await signCompactEd25519(kp.privateKey, header, payload);
}
