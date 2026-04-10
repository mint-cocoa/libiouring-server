import { sleep } from 'k6';
import { thresholds } from './config.js';
import {
  getHello, getJson, postEcho, getUser,
  getStaticSmall,
} from './helpers/requests.js';

export const options = {
  thresholds: thresholds.stress,
  scenarios: {
    stress: {
      executor: 'ramping-vus',
      stages: [
        { duration: '10s', target: 100 },
        { duration: '30s', target: 100 },
        { duration: '30s', target: 300 },
        { duration: '30s', target: 300 },
        { duration: '30s', target: 500 },
        { duration: '30s', target: 500 },
        { duration: '10s', target: 0 },
      ],
    },
  },
};

export default function () {
  const roll = Math.random();

  if (roll < 0.4) {
    getHello();
  } else if (roll < 0.6) {
    getJson();
  } else if (roll < 0.8) {
    postEcho({ vu: __VU, i: __ITER });
  } else if (roll < 0.9) {
    getUser((__VU * 100 + __ITER) % 10000);
  } else {
    getStaticSmall();
  }

  sleep(0.01);
}
