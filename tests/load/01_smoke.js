import { sleep } from 'k6';
import { thresholds } from './config.js';
import {
  getHello, getJson, postEcho, getUser,
  getWildcard, getStaticSmall, getStaticMedium,
} from './helpers/requests.js';

export const options = {
  thresholds: thresholds.smoke,
  scenarios: {
    smoke: {
      executor: 'ramping-vus',
      stages: [
        { duration: '5s', target: 1 },
        { duration: '15s', target: 5 },
        { duration: '5s', target: 5 },
        { duration: '5s', target: 0 },
      ],
    },
  },
};

export default function () {
  getHello();
  getJson();
  postEcho({ test: 'smoke', iteration: __ITER });
  getUser(Math.floor(Math.random() * 100) + 1);
  getWildcard('docs/readme.txt');
  getStaticSmall();
  getStaticMedium();
  sleep(0.5);
}
