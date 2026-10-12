/* Normalized Tunah specifications, after EkippX preprocessing. */
${declare longest_match Document}
Document: Declaration*;
Declaration: Operator | Rule;
Operator: '(' 'operator' Symbol Unsigned ')';
Rule: '(' 'rule' Symbol Term Term Clause* ')';
Clause: Where | Cost;
Where: ':where' Term;
Cost: ':cost' Integer;
Term: Application | Integer | Variable | Symbol;
Application: '(' Symbol Term* ')';
Variable: "\?[A-Za-z_][A-Za-z0-9_.-]*";
Integer: "-?[0-9]+";
Unsigned: "[0-9]+";
Symbol: "[A-Za-z_+*/=<>!-][A-Za-z0-9_+*/=<>!?.:-]*" $term -1;
whitespace: "([ \t\r\n]|;[^\n]*)*";
