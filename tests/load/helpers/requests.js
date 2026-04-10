import http from 'k6/http';
import { check } from 'k6';
import { BASE_URL } from '../config.js';

export function getHello() {
  const res = http.get(`${BASE_URL}/api/demo/hello`);
  check(res, { 'hello 200': (r) => r.status === 200 });
  return res;
}

export function getJson() {
  const res = http.get(`${BASE_URL}/api/demo/json`);
  check(res, { 'json 200': (r) => r.status === 200 });
  return res;
}

export function postEcho(payload) {
  const body = JSON.stringify(payload || { ts: Date.now(), data: 'k6-load-test' });
  const res = http.post(`${BASE_URL}/api/demo/echo`, body, {
    headers: { 'Content-Type': 'application/json' },
  });
  check(res, { 'echo 200': (r) => r.status === 200 });
  return res;
}

export function getUser(id) {
  const res = http.get(`${BASE_URL}/api/demo/users/${id}`);
  check(res, { 'user 200': (r) => r.status === 200 });
  return res;
}

export function getWildcard(path) {
  const res = http.get(`${BASE_URL}/api/demo/files/${path}`);
  check(res, { 'wildcard 200': (r) => r.status === 200 });
  return res;
}

export function getStaticSmall() {
  const res = http.get(`${BASE_URL}/bench/small.html`);
  check(res, { 'static-small 200': (r) => r.status === 200 });
  return res;
}

export function getStaticMedium() {
  const res = http.get(`${BASE_URL}/bench/medium.html`);
  check(res, { 'static-medium 200': (r) => r.status === 200 });
  return res;
}

export function getStaticLarge() {
  const res = http.get(`${BASE_URL}/bench/large.html`);
  check(res, { 'static-large 200': (r) => r.status === 200 });
  return res;
}
