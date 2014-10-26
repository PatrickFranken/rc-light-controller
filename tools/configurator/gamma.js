"use strict";

var gamma = (function () {

    // *************************************************************************
    var calculate_single_value = function (level, gamma) {
        return Math.round(255 * Math.pow(level / 255, gamma));
    };


    // *************************************************************************
    var calculate_gamma_table = function (gamma_value) {
        var gamma_table = [];

        gamma_value = parseFloat(gamma_value);

        for (var i = 0; i < 256; i++) {
            gamma_table.push(calculate_single_value(i, gamma_value));
        }
        return gamma_table;
    };


    // *************************************************************************
    return {
        make_table: calculate_gamma_table
    };
})();


// node.js exports; hide from browser where exports is undefined and use strict
// would trigger.
if (typeof exports !== 'undefined') {
    exports.gamma = gamma;
}