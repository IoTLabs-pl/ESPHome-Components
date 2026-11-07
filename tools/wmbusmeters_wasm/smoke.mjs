// Every path through the wrapper runs without throwing: metadata, plain, AES, an error, frames past the 64 KB stack.
import assert from "node:assert/strict";

const decoder = await (await import(process.argv[2])).default();
const IPERL = "1844AE4C4455223368077A55000000041389E20100023B0000";
const KADEN =
  "4e443059040680231e087ac40040050e6aa476257c0c3adae8277edc999b39b38222fcb387a91e94cb6ed47ceec6470f5f686f89a8574415fa262bd43c88f7f153ce3c66e8e44da338a06c62ab21b1";
const decode = (telegrams, key) => decoder.decode({ key, telegrams });

assert(decoder.drivers.iperl);
assert(decode([IPERL]).json);
assert(decode([KADEN], "82B0551191F51D66EFCDAB8967452301").json);
assert.equal(decode([KADEN]).error, "no_key");
assert(decode(Array(2000).fill(IPERL)).json);
