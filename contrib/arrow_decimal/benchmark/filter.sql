SELECT count(*)
FROM :table
WHERE v BETWEEN CAST(':low' AS :type) AND CAST(':high' AS :type);
