import { thresholds } from './config.js';
import { getHello, getJson } from './helpers/requests.js';

export const options = {
  thresholds: thresholds.breakpoint,
  scenarios: {
    breaking: {
      executor: 'ramping-vus',
      stages: [
        { duration: '10s', target: 100 },
        { duration: '10s', target: 200 },
        { duration: '10s', target: 500 },
        { duration: '10s', target: 1000 },
        { duration: '10s', target: 2000 },
        { duration: '10s', target: 5000 },
      ],
    },
  },
};

export default function () {
  if (Math.random() < 0.5) {
    getHello();
  } else {
    getJson();
  }
}
