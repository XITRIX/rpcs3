'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const scriptPath = path.join(__dirname, '..', 'Resources', 'RPCS3UniversalJIT26.js');
const script = fs.readFileSync(scriptPath, 'utf8');
const protocolMagic = 0x5253n;
const protocolResponse = 0x52504353334a4954n;

function encodeLE64(value) {
    let remaining = BigInt.asUintN(64, value);
    let result = '';
    for (let index = 0; index < 8; ++index) {
        result += Number(remaining & 0xffn).toString(16).padStart(2, '0');
        remaining >>= 8n;
    }
    return result;
}

function runProtocolTest({ instruction, stop }) {
    const commands = [];
    const logs = [];
    let continues = 0;

    const context = {
        get_pid: () => 42,
        log: message => logs.push(String(message)),
        prepare_memory_region: () => {
            throw new Error('prepare_memory_region was not expected');
        },
        send_command: command => {
            commands.push(command);
            if (command.startsWith('vAttach;')) return 'T11thread:7;';
            if (command === 'c') return continues++ === 0 ? stop : 'W00';
            if (command.startsWith('m')) return instruction;
            if (command.startsWith('p11;')) return encodeLE64(0x5253n);
            if (command.startsWith('P')) return 'OK';
            if (command.startsWith('vCont;')) return 'W00';
            throw new Error(`Unexpected debugger command: ${command}`);
        },
    };

    vm.runInNewContext(script, context, { filename: scriptPath });
    return { commands, logs };
}

const pc = 0x100000n;
const pingStop = [
    'T05',
    'thread:7',
    `20:${encodeLE64(pc)}`,
    `10:${encodeLE64((protocolMagic << 16n) | 1n)}`,
    // Deliberately omit x17. Protocol v2 carries the magic in expedited x16.
].join(';') + ';';

const ping = runProtocolTest({ instruction: '004a2ad4', stop: pingStop });
assert(!ping.commands.includes('p11;thread:7;'));
assert(ping.commands.includes(`P0=${encodeLE64(protocolResponse)};thread:7;`));
assert(ping.commands.includes(`P20=${encodeLE64(pc + 4n)};thread:7;`));
assert(ping.logs.includes('RPCS3 JIT: handshake accepted'));

const legacyPingStop = pingStop.replace(
    encodeLE64((protocolMagic << 16n) | 1n),
    encodeLE64(1n),
);
const legacyPing = runProtocolTest({ instruction: '004a2ad4', stop: legacyPingStop });
assert(legacyPing.commands.includes('p11;thread:7;'));
assert(legacyPing.commands.includes(`P0=${encodeLE64(protocolResponse)};thread:7;`));

const unrelated = runProtocolTest({ instruction: '1f2003d5', stop: pingStop });
assert(unrelated.commands.includes('vCont;S05:7'));
assert(!unrelated.commands.some(command => command.startsWith('P0=')));

console.log('RPCS3UniversalJIT26Tests passed');
