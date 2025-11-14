// api/db.js
import { apiGet, apiPut } from "../api/client";
import { canonicalizeEmail } from "../utils/email";

const API_BASE = "/api";

export async function db_add_user(email) {
  const c = canonicalizeEmail(email);
  if (!c.ok) throw new Error("Invalid email");
  return apiPut(`${API_BASE}/db_add_user`, { email: c.value });
}

export async function db_list_users() {
  return apiGet(`${API_BASE}/db_list_users`);
}
