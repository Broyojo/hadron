const source = 'function hasOwnProperty() { [native code] }';
const expected = 'function hasOwnProperty\\(\\) \\{ \\[native code\\] \\}';
const metacharacters = /[\\^$.*+?()[\]{}|]/g;

for (let iteration = 0; iteration < 10000; ++iteration) {
    const escaped = source.replace(metacharacters, '\\$&');
    if (escaped !== expected)
        throw new Error(JSON.stringify({iteration, escaped, expected}));
    const pattern = '^' + escaped.replace(
        /hasOwnProperty|(function).*?(?=\\\()| for .+?(?=\\\])/g, '$1.*?') + '$';
    if (!new RegExp(pattern).test(source)) throw new Error('pattern did not match');
}
