const path = require('path');

module.exports = {
  target: 'node',
  entry: './staging/index.js',
  output: {
    libraryTarget: 'commonjs2',
    path: __dirname,
    filename: 'index.js'
  },
};
