CREATE TABLE foo (i INT);
INSERT INTO foo (i) VALUES (NULL);
DELETE from foo where i IS NULL;
ALTER TABLE foo ALTER COLUMN i SET NOT NULL;
DROP TABLE foo;
