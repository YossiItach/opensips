#!/bin/bash
# loads a carrierroute config for loadbalancing from postgres database

# Copyright (C) 2007 1&1 Internet AG
#
# This file is part of openser, a free SIP server.
#
# openser is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version
#
# openser is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

CFG=14.cfg

if [ ! -e ../modules/carrierroute/carrierroute.so ] ; then
	echo "carrierroute not found, not run"
	exit 0
fi ;

cp $CFG $CFG.bak

# setup config
echo "loadmodule \"db_postgres/db_postgres.so\"" >> $CFG
echo "modparam(\"carrierroute\", \"config_source\", \"db\")" >> $CFG
echo "modparam(\"carrierroute\", \"db_url\", \"postgres://openserro:openserro@localhost/openser\")" >> $CFG

# setup database
PGPASSWORD='openserrw' psql -A -t -n -q -h localhost -U openser openser -c "insert into route_tree (id, carrier) values ('1', 'carrier1');
insert into route_tree (id, carrier) values ('2', 'default');
insert into carrierroute (id, carrier, scan_prefix, domain, prob, strip, rewrite_host) values ('1','1','49','0','0.5','0','host1.local');
insert into carrierroute (id, carrier, scan_prefix, domain, prob, strip, rewrite_host) values ('2','1','49','0','0.5','0','host2.local');
insert into carrierroute (id, carrier, scan_prefix, domain, prob, strip, rewrite_host) values ('3','1','42','0','0.3','0','host3.local');
insert into carrierroute (id, carrier, scan_prefix, domain, prob, strip, rewrite_host) values ('4','1','42','0','0.7','0','host4.local');
insert into carrierroute (id, carrier, scan_prefix, domain, prob, strip, rewrite_host) values ('5','1','','0','0.1','0','host5.local');
insert into carrierroute (id, carrier, scan_prefix, domain, prob, strip, rewrite_host) values ('6','2','','0','1','0','host6.local');"

../openser -w . -f $CFG > /dev/null
ret=$?

sleep 1

cd ../scripts

TMPFILE=`mktemp -t openser-test.XXXXXXXXXX`

if [ "$ret" -eq 0 ] ; then
	./openserctl fifo cr_dump_routes > $TMPFILE
	ret=$?
fi ;

if [ "$ret" -eq 0 ] ; then
	tmp=`grep -v "Printing routing information:
Printing tree for carrier carrier1 (1)
Printing tree for domain 0
        42: 70.140 %, 'host4.local': ON, '0', '', '', ''
        42: 30.060 %, 'host3.local': ON, '0', '', '', ''
        49: 50.000 %, 'host2.local': ON, '0', '', '', ''
        49: 50.000 %, 'host1.local': ON, '0', '', '', ''
      NULL: 100.000 %, 'host5.local': ON, '0', '', '', ''
Printing tree for carrier default (2)
Printing tree for domain 0
      NULL: 100.000 %, 'host6.local': ON, '0', '', '', ''" $TMPFILE`
	if [ "$tmp" = "" ] ; then
		ret=0
	else
		ret=1
	fi ;
fi ;

killall -9 openser

# cleanup database
PGPASSWORD='openserrw' psql -A -t -n -q -h localhost -U openser openser -c "delete from route_tree where id = 1;
delete from route_tree where id = 2;
delete from carrierroute where carrier=1;
delete from carrierroute where carrier=2;"

cd ../test

mv $CFG.bak $CFG
rm $TMPFILE

exit $ret