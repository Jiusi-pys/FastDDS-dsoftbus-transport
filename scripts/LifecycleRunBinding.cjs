'use strict';

// Pure consistency only; Main independently pins expected and gathers raw data.
// This is not permission, authentication, a lock, live state or device evidence.
const {validateLifecycleRunBinding} = require('./RunBindingCore.cjs');
module.exports = {validateLifecycleRunBinding};
