import { sha256Bytes, bytesToHex, solveAuthChallenge } from './sha256';

describe('sha256 util', () => {
  const hash = (text: string) => bytesToHex(sha256Bytes(new TextEncoder().encode(text)));

  it('matches known SHA-256 vectors', () => {
    expect(hash('')).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
    expect(hash('abc')).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
    expect(hash('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'))
      .toBe('248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
  });

  it('derives a deterministic challenge response', () => {
    const salt = '00000000000000000000000000000000';
    const challenge = '0100000000000000000000000000000000000000000000000000000000000000';
    const first = solveAuthChallenge('secreta', salt, challenge);
    const second = solveAuthChallenge('secreta', salt, challenge);
    const other = solveAuthChallenge('otra', salt, challenge);

    expect(first).toBe(second);
    expect(first).not.toBe(other);
    expect(first.length).toBe(64);
  });
});
