INSERT INTO version (table_name, table_version) values ('dispatcher','3');
CREATE TABLE dispatcher (
    id INT(10) UNSIGNED AUTO_INCREMENT PRIMARY KEY NOT NULL,
    setid INT DEFAULT 0 NOT NULL,
    destination CHAR(192) DEFAULT '' NOT NULL,
    flags INT DEFAULT 0 NOT NULL,
    description CHAR(64) DEFAULT '' NOT NULL
) ENGINE=MyISAM;

