'use strict';

// Compatibility entry point. No caller-configurable policy or launch behavior.
const {validateRecordRunBinding} = require('./RunBindingCore.cjs');
module.exports = {validateRecordRunBinding};
