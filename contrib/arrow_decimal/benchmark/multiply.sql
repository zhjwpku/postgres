SELECT sum(v * CAST('1.01' AS :type))
FROM :table;
