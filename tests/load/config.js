export const BASE_URL = __ENV.BASE_URL || 'http://localhost:8080';

export const thresholds = {
  smoke: {
    http_req_failed: ['rate<0.01'],
    http_req_duration: ['p(95)<200'],
  },
  average: {
    http_req_failed: ['rate<0.01'],
    http_req_duration: ['p(95)<50', 'p(99)<100'],
  },
  stress: {
    http_req_failed: ['rate<0.05'],
    http_req_duration: ['p(95)<100', 'p(99)<300'],
  },
  breakpoint: {
    http_req_failed: [{ threshold: 'rate<0.01', abortOnFail: true }],
    http_req_duration: ['p(99)<500'],
  },
};
