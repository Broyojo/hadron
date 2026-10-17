import {app} from 'electron';
import fs from 'node:fs';

const result = 'C:\\hadron-regexp-replace-result.json';
fs.writeFileSync(result, JSON.stringify({status:'started', time:new Date().toISOString()}));
setTimeout(() => app.exit(2), 30000);
try {
    await import('./probe.mjs');
    fs.writeFileSync(result, JSON.stringify({status:'PASS', iterations:10000}));
    app.whenReady().then(() => app.exit(0));
} catch (error) {
    fs.writeFileSync(result, JSON.stringify({status:'FAIL', message:error.message}));
    app.exit(1);
}
