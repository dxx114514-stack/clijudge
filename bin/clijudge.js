#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const path = require('path');
const os = require('os');
const fs = require('fs');

const root = path.join(__dirname, '..');

let bin;
if (process.platform === 'win32') {
  bin = path.join(root, 'prebuilds', 'windows', 'clijudge.exe');
} else if (process.platform === 'linux') {
  bin = path.join(root, 'prebuilds', 'linux', 'clijudge-linux.bin');
} else {
  console.error('clijudge: unsupported platform "' + process.platform + '" (supported: win32, linux)');
  process.exit(1);
}

if (!fs.existsSync(bin)) {
  console.error('clijudge: binary missing: ' + bin);
  process.exit(1);
}

// 默认数据目录放到用户目录, 避免 npm 升级时清空题目数据;
// 已设置 CLIJUDGE_DATA_DIR 时完全尊重用户选择
if (!process.env.CLIJUDGE_DATA_DIR) {
  if (process.platform === 'win32') {
    const base = process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local');
    process.env.CLIJUDGE_DATA_DIR = path.join(base, 'clijudge', 'data');
  } else {
    const base = process.env.XDG_DATA_HOME || path.join(os.homedir(), '.local', 'share');
    process.env.CLIJUDGE_DATA_DIR = path.join(base, 'clijudge', 'data');
  }
}

if (process.platform !== 'win32') {
  try {
    fs.chmodSync(bin, 0o755);
  } catch (e) {
    /* best effort */
  }
}

const result = spawnSync(bin, process.argv.slice(2), { stdio: 'inherit' });
if (result.error) {
  console.error('clijudge: failed to launch: ' + result.error.message);
  process.exit(1);
}
process.exit(result.status === null ? 1 : result.status);
