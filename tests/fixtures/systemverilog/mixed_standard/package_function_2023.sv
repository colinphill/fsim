// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2023. foreach over a string is a 1800-2023
// feature, so the body must be lowered under this package's own standard
// when a 2017 module calls it.
package p23;
  function automatic int count_a(string s);
    int n = 0;
    foreach (s[i])
      if (s[i] == "a")
        n++;
    return n;
  endfunction
endpackage
