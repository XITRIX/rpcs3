// RPCS3 iOS 26 debugger-assisted JIT protocol.
//
// This script is an independent implementation for debugger hosts exposing
// get_pid(), send_command(), prepare_memory_region(), and log(). It deliberately
// recognizes only RPCS3's exact BRK instruction and register magic. Every other
// stop is delivered back to the debuggee unchanged.

'use strict';

const RPCS3_BRK_IMMEDIATE = 0x5250;
const RPCS3_BRK_INSTRUCTION = (0xd4200000 | (RPCS3_BRK_IMMEDIATE << 5)) >>> 0;
const RPCS3_PROTOCOL_MAGIC = 0x5253n;
const RPCS3_PROTOCOL_RESPONSE = 0x52504353334a4954n;
const RPCS3_COMMAND_PING = 1n;
const RPCS3_COMMAND_PREPARE = 2n;

function hexRegister(stop, registerName) {
    const expression = new RegExp('(?:^|;)' + registerName + ':([0-9a-fA-F]{16})(?:;|$)');
    const match = expression.exec(stop);
    return match ? match[1] : null;
}

function decodeLE64(value) {
    if (!/^[0-9a-fA-F]{16}$/.test(value || '')) {
        return null;
    }

    let result = 0n;
    for (let offset = 14; offset >= 0; offset -= 2) {
        result = (result << 8n) | BigInt(parseInt(value.slice(offset, offset + 2), 16));
    }
    return result;
}

function encodeLE64(value) {
    let remaining = BigInt.asUintN(64, value);
    let result = '';
    for (let index = 0; index < 8; ++index) {
        result += Number(remaining & 0xffn).toString(16).padStart(2, '0');
        remaining >>= 8n;
    }
    return result;
}

function decodeLE32(value) {
    if (!/^[0-9a-fA-F]{8}$/.test(value || '')) {
        return null;
    }

    return (parseInt(value.slice(6, 8), 16) << 24 |
            parseInt(value.slice(4, 6), 16) << 16 |
            parseInt(value.slice(2, 4), 16) << 8 |
            parseInt(value.slice(0, 2), 16)) >>> 0;
}

function stopInfo(stop) {
    const signalMatch = /^T([0-9a-fA-F]{2})/.exec(stop || '');
    const threadMatch = /(?:^|;)thread:([0-9a-fA-F]+)(?:;|$)/.exec(stop || '');
    if (!signalMatch || !threadMatch) {
        return null;
    }

    return { signal: signalMatch[1].toLowerCase(), thread: threadMatch[1] };
}

function writeRegister(thread, registerNumber, value) {
    return send_command(`P${registerNumber.toString(16)}=${encodeLE64(value)};thread:${thread};`);
}

function readRegister(stop, thread, registerNumber) {
    const registerName = registerNumber.toString(16).padStart(2, '0');
    let encoded = hexRegister(stop, registerName);

    // Apple's stop reply contains only its current expedited-register set.
    // In particular, x17 is not guaranteed to be present even though RPCS3
    // uses it as the protocol discriminator. Read any omitted register from
    // the stopped thread instead of treating the stop as unrelated.
    if (encoded === null) {
        encoded = send_command(`p${registerNumber.toString(16)};thread:${thread};`);
    }

    const value = decodeLE64(encoded);
    if (value === null) {
        log(`RPCS3 JIT: could not read register ${registerName} on thread ${thread}`);
    }
    return value;
}

function forwardStop(stop, info) {
    log(`RPCS3 JIT: forwarding signal 0x${info.signal} on thread ${info.thread}`);
    return send_command(`vCont;S${info.signal}:${info.thread}`);
}

function handleRPCS3Stop(stop, info) {
    const pc = readRegister(stop, info.thread, 0x20);
    if (pc === null) {
        return false;
    }

    const instruction = decodeLE32(send_command(`m${pc.toString(16)},4`));
    if (instruction !== RPCS3_BRK_INSTRUCTION) {
        return false;
    }

    const command = readRegister(stop, info.thread, 0x10);
    const magic = readRegister(stop, info.thread, 0x11);
    if (command === null || magic !== RPCS3_PROTOCOL_MAGIC) {
        return false;
    }

    let result;
    if (command === RPCS3_COMMAND_PING) {
        result = RPCS3_PROTOCOL_RESPONSE;
        log('RPCS3 JIT: handshake accepted');
    } else if (command === RPCS3_COMMAND_PREPARE) {
        const address = readRegister(stop, info.thread, 0x00);
        const length = readRegister(stop, info.thread, 0x01);
        if (address === null || length === null) {
            return false;
        }

        if (address === 0n || length === 0n) {
            result = 0n;
        } else {
            try {
                prepare_memory_region(address, length);
                result = address;
            } catch (error) {
                log(`RPCS3 JIT: executable-region preparation failed: ${String(error)}`);
                result = 0n;
            }
        }
    } else {
        // Unknown RPCS3 protocol versions or commands are left at the original
        // instruction and receive the original signal.
        return false;
    }

    writeRegister(info.thread, 0, result);
    writeRegister(info.thread, 0x20, pc + 4n);
    return true;
}

const rpcPid = get_pid();
log(`RPCS3 JIT: attaching to pid ${rpcPid}`);
let stop = send_command(`vAttach;${rpcPid.toString(16)}`);
log(`RPCS3 JIT: attach response = ${stop}`);

// vAttach stops the process before any RPCS3 command is pending.
stop = send_command('c');
for (;;) {
    const info = stopInfo(stop);
    if (!info) {
        if (/^[WX]/.test(stop || '')) {
            log(`RPCS3 JIT: process ended (${stop})`);
            break;
        }
        stop = send_command('c');
        continue;
    }

    if (handleRPCS3Stop(stop, info)) {
        stop = send_command('c');
    } else {
        stop = forwardStop(stop, info);
    }
}
