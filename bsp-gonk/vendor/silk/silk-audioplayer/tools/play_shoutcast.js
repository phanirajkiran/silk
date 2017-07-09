/**
 * @flow
 */

import Player from 'silk-audioplayer';
import icy from 'icy';
import createLog from 'silk-log';
import {sleep, createDeferred} from 'silk-async-utils';
const log = createLog('test');

let url = 'http://kidspublicradio2.got.net:8000/lullaby?lang=en-US%2cen%3bq%3d0.8';

let player = new Player();
let shoutcastResponse = null;
let deferred = null;

function playShoutcast() {
  const onEnd = (error) => {
    if (error) {
      log.error(error.message);
    }
    log.info(`Shoutcast stream ended`);
    if (deferred) {
      deferred.accept();
    }
    if (player) {
      player.removeAllListeners('error');
      player.removeAllListeners('started');
    }
  };

  return new Promise((resolve, reject) => {
    // Connect to the remote stream
    icy.get(url, function (res) {
      shoutcastResponse = res;
      log.info(`headers: `, res.headers);

      // Log any "metadata" events that happen
      res.once('metadata', (metadata) => {
        let parsed = icy.parse(metadata);
        log.info(`parsed: `, parsed);
      });

      player.on('error', (err) => {
        log.info('error');
        log.error(err);
        reject(err);
      });
      player.on('started', () => {
        log.info(`Player started`);
        resolve();
      });

      res.on('data', (data) => {
        player.write(data);
      });
      res.once('error', onEnd);
      res.once('end', onEnd);
    });
  });
}

function stopShoutcast() {
  deferred = createDeferred();
  if (shoutcastResponse) {
    log.info(`Stopping shoutcast`);
    shoutcastResponse.res.client.destroy();
    shoutcastResponse = null;
  }

  return deferred.promise
  .then(() => {
    deferred = null;
    log.info(`Stopping audio player`);
    return player.stop();
  });
}

async function test() {
  for (;;) {
    await playShoutcast();
    await sleep(2000);
    await stopShoutcast();
  }
}

test()
.then(() => log.info(`done`))
.catch((err) => log.error(`Error: `, err));
