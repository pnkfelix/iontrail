// Adapted from lattice.sch (by Andrew Wright).

// Given a comparison routine that returns one of
//  "less"
//  "more"
//  "equal"
//  "uncomparable"
// return a new comparison routine that applies to sequences.

load(libdir + "util.js");

function lexico(base) {
  function lex_fixed(fixed, lhs, rhs) {
    return check(lhs, rhs);

    function check(lhs, rhs) {
      while (true) {
        if (lhs === null) {
          return fixed;
        } else {
          var probe = base(lhs.car, rhs.car);
          if (probe === "equal" || probe === fixed) {
            lhs = lhs.cdr; rhs = rhs.cdr;
            continue;
          } else {
            return "uncomparable";
          }
        }
      }
    }
  }
  function lex_first(lhs, rhs) {
    // print(JSON.stringify({name:"lex_first", lhs:lhs, rhs:rhs}));
    if (lhs === null) {
      return "equal";
    } else {
      var probe = base(lhs.car, rhs.car);
      switch (probe) {
        case "less": case "more":
            return lex_fixed(probe, lhs.cdr, rhs.cdr);
        case "equal":
            return lex_first(lhs.cdr, rhs.cdr);
        default:
        return "uncomparable";
      }
    }
  }
  return lex_first;
}

function make_lattice(elem_list, cmp_func) {
  return cons(elem_list, cmp_func);
}

function lattice_to_elements(x) { return x.car; }
function lattice_to_cmp(x) { return x.cdr; }

function zulu_select(test, lst) {
  return select_a(null, lst);

  function select_a(ac, lst) {
    if (lst === null) {
      return reverse_inplace(ac);
    } else {
      var head = lst.car;
      var arg = test(head) ? cons(head, ac) : ac;
      return select_a(arg, lst.cdr);
    }
  }
}

function select_map(test, func, lst) {
  return select_a(null, lst);

  function select_a(ac, lst) {
    while (true) {
      if (lst === null)
        return reverse_inplace(ac);
      var head = lst.car;
      var arg = test(head) ? cons(func(head), ac) : ac;
      ac = arg;
      lst = lst.cdr;
    }
  }
}

function map_and(proc, lst) {
  if (lst === null) {
    return true;
  } else {
    while (true) {
      var rest = lst.cdr;
      if (rest === null)
        return proc(lst.car);
      if (!proc(lst.car))
        return false;
      lst = rest;
    }
  }
}

function maps_1(source, target, pas, new_) {
  var scmp = lattice_to_cmp(source);
  var tcmp = lattice_to_cmp(target);
  var less = select_map(function (p) { return scmp(p.car, new_) === "less"; }, cdr, pas);
  var more = select_map(function (p) { return scmp(p.car, new_) === "more"; }, cdr, pas);
  return zulu_select(function (t) {
                       return (map_and(t2 => memq(tcmp(t2, t), cons("less", cons("equal", null))), less)
                               &&
                               map_and(t2 => memq(tcmp(t2, t), cons("more", cons("equal", null))), more));
                     }, lattice_to_elements(target));
}

function maps_rest(source, target, pas, rest, to_1, to_collect) {
  if (rest === null) {
    return to_1(pas);
  } else {
    var next = rest.car;
    var rest = rest.cdr;
    return to_collect(map((x) => maps_rest(source, target, cons(cons(next,x), pas), rest, to_1, to_collect),
                          maps_1(source, target, pas, next)));
  }
}

function maps(source, target) {
  return make_lattice(maps_rest(source,
                                target,
                                null,
                                lattice_to_elements(source),
                                (x) => cons(map(cdr,x),null),
                                (x) => apply(append, x)),
                      lexico(lattice_to_cmp(target)));
}

// var print_frequency = 10000;
var print_frequency = 1000;

function count_maps(source, target) {
  var count = 0;
  return maps_rest(source,
                   target,
                   null,
                   lattice_to_elements(source),
                   function (x) {
                     count = count + 1;
                     if (count % print_frequency === 0) {
                       // print(JSON.stringify(x));
                     }
                     return 1;
                   },
                   (x) => apply(plus, x));
}

function lattice_benchmark() {
  return run_benchmark("lattice",
                function () {
                  var l2 = make_lattice(cons("low", cons("high", null)),
                                        function (lhs, rhs) {
                                          switch (lhs) {
                                            case "low": switch (rhs) {
                                                case "low": return "equal";
                                                case "high": return "less";
                                                default: throw new Exception("make_lattice", "base", rhs);
                                            }
                                            case "high": switch (rhs) {
                                                case "low": return "more";
                                                case "high": return "equal";
                                                default: throw new Exception("make_lattice", "base", rhs);
                                            }
                                            default: throw new Exception("make_lattice", "base", lhs);
                                          }});
                  var l3 = maps(l2, l2);
                  var l4 = maps(l3, l3);
                count_maps(l2, l2);
                count_maps(l3, l3);
                count_maps(l2, l3);
                count_maps(l3, l2);
                try { count_maps(l4, l4); } catch (e) { }
                try { count_maps(l4, l4); } catch (e) { }
                },
                1);
}

print(JSON.stringify(lattice_benchmark(10)));
