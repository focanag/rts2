-- sequence for filters
CREATE SEQUENCE filter_id;

-- change filter_id from VARCHAR to integer, clear DB design
ALTER TABLE images ADD COLUMN filter_id integer;
DROP INDEX fk_images_filters;
ALTER TABLE filters RENAME COLUMN filter_id TO f_id;

ALTER TABLE filters ADD COLUMN filter_id integer;
UPDATE filters SET filter_id = nextval('filter_id');
UPDATE images SET filter_id = (SELECT filter_id FROM filters WHERE f_id = img_filter);

ALTER TABLE filters DROP COLUMN f_id CASCADE;
ALTER TABLE images DROP COLUMN img_filter CASCADE;

CREATE UNIQUE INDEX fk_filters ON filters(filter_id);
ALTER TABLE images ADD CONSTRAINT "fk_images_filters2" FOREIGN KEY (filter_id) REFERENCES filters(filter_id);
CREATE UNIQUE INDEX index_filter_names ON filters(standart_name);

-- recreate views dropped during upgrade
CREATE VIEW observations_noimages AS 
SELECT observations.obs_id, 0 AS img_count 
	FROM observations 
	WHERE (NOT (EXISTS 
		(SELECT * FROM images 
			WHERE observations.obs_id = images.obs_id)));

CREATE VIEW observations_images AS 
SELECT obs_id, img_count 
	FROM observations_imgcount 
UNION SELECT obs_id, img_count 
	FROM observations_noimages;

CREATE VIEW images_nights AS
SELECT images.*, date_part('day', (timestamptz(images.img_date) - interval '12:00')) AS img_night, 
	date_part('month', (timestamptz(images.img_date) - interval '12:00')) AS img_month, 
	date_part('year', (timestamptz(images.img_date) - interval '12:00')) AS img_year 
	FROM images;

CREATE TABLE schedules (
	queue_id    integer NOT NULL,
	tar_id 	    integer REFERENCES targets(tar_id),
	time_start  timestamp,
	time_end    timestamp
);