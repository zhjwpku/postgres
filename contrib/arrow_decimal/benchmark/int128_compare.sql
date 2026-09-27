SELECT count(*)
FROM ad_int128
WHERE :operator(v, '-25.0000') >= 0
  AND :operator(v, '25.0000') <= 0;
