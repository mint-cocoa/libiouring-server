import { sleep } from 'k6';
import { thresholds } from './config.js';
import {
  getHello, getJson, postEcho, getUser,
  getWildcard, getStaticSmall, getStaticMedium,
} from './helpers/requests.js';

export const options = {
  thresholds: thresholds.average,
  scenarios: {
    average_load: {
      executor: 'ramping-vus',
      stages: [
        { duration: '10s', target: 20 },
        { duration: '30s', target: 50 },
        { duration: '1m', target: 50 },
        { duration: '10s', target: 0 },
      ],
    },
  },
};

export default function () {
  const roll = Math.random();

  if (roll < 0.3) {
    // 30% - minimal GET
    getHello();
  } else if (roll < 0.5) {
    // 20% - JSON GET
    getJson();
  } else if (roll < 0.7) {
    // 20% - POST echo
    postEcho({ vuid: __VU, iter: __ITER, ts: Date.now() });
  } else if (roll < 0.8) {
    // 10% - parameterized route
    getUser(Math.floor(Math.random() * 1000) + 1);
  } else if (roll < 0.9) {
    // 10% - wildcard route
    getWildcard(`assets/img/photo_${__ITER}.jpg`);
  } else {
    // 10% - static files
    if (Math.random() < 0.7) {
      getStaticSmall();
    } else {
      getStaticMedium();
    }
  }

  sleep(0.05);
}
