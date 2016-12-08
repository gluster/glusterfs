#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

brick_port() {
        $CLI --xml volume status $1 | sed -n '/.*<port>\([0-9]*\).*/s//\1/p'
}

wait_mount() {
	i=1
	while [ $i -lt $CONFIG_UPDATE_TIMEOUT ] ; do
		sleep 1
		i=$(( $i + 1 ))
		mounted=`mount|awk -v m=$1 '
				BEGIN {r = "N";}
				($3 == m) {r = "Y"; exit;}
				END {print r;}
		'`
		if [ "x${mounted}" = "xY" ] ; then
			ls $M0 2>/dev/null || continue
			break;
		fi
	done

	if [ "x${mounted}" = "xY" ] ; then
		ls $M0 2>/dev/null || mounted="N"
	fi

	echo $mounted
}

openssl_connect() {
	ssl_opt="-verify 3 -verify_return_error -CAfile $SSL_CA"
	ssl_opt="$ssl_opt -crl_check_all -CApath $TMPDIR"
	#echo openssl s_client $ssl_opt $@ > /dev/tty
	#read -p "Continue? " nothing
	CIPHER=`echo "" |
                openssl s_client $ssl_opt $@ 2>/dev/null |
		awk '/^    Cipher/{print $3}'`
	if [ "x${CIPHER}" = "x" -o "x${CIPHER}" = "x0000" ] ; then
		echo "N"
	else
		echo "Y"
	fi
}

cleanup;
mkdir -p $B0
mkdir -p $M0

TMPDIR=`mktemp -d /tmp/${0##*/}.XXXXXX`
TEST test -d $TMPDIR

SSL_KEY=$TMPDIR/self.key
SSL_CSR=$TMPDIR/self.csr
SSL_CERT=$TMPDIR/self.crt
SSL_CA=$TMPDIR/ca.crt
SSL_CFG=$TMPDIR/openssl.cnf
SSL_CRL=$TMPDIR/crl.pem

sed "s|@TMPDIR@|${TMPDIR}|" `pwd`/`dirname $0`/openssl.cnf.in > $SSL_CFG

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST openssl genrsa -out $SSL_KEY 1024 2>/dev/null
TEST openssl req -config $SSL_CFG -new -key $SSL_KEY -x509 \
                  -subj /CN=CA -out $SSL_CA
TEST openssl req -config $SSL_CFG -new -key $SSL_KEY \
                  -subj /CN=$H0 -out $SSL_CSR

echo "01" > $TMPDIR/serial
TEST touch $TMPDIR/index.txt $TMPDIR/index.txx.attr
TEST mkdir -p $TMPDIR/certs $TMPDIR/newcerts $TMPDIR/crl
TEST openssl ca -batch -config $SSL_CFG -in $SSL_CSR -out $SSL_CERT 2>&1

touch $SSL_CRL
CRLHASH=`openssl x509 -hash -fingerprint -noout -in $SSL_CA|sed -n '1s/$/.r0/p'`
ln -sf $SSL_CRL $TMPDIR/$CRLHASH
TEST openssl ca -config $SSL_CFG -gencrl -out $SSL_CRL 2>&1


TEST $CLI volume create $V0 $H0:$B0/1
TEST $CLI volume set $V0 server.ssl on
TEST $CLI volume set $V0 client.ssl on
TEST $CLI volume set $V0 ssl.private-key $SSL_KEY
TEST $CLI volume set $V0 ssl.own-cert $SSL_CERT
TEST $CLI volume set $V0 ssl.ca-list $SSL_CA
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count

BRICK_PORT=`brick_port $V0`

# Test we can connect
EXPECT "Y" openssl_connect -connect $H0:$BRICK_PORT

# Test SSLv2 protocol fails
EXPECT "N" openssl_connect -ssl2 -connect $H0:$BRICK_PORT

# Test SSLv3 protocol fails
EXPECT "N" openssl_connect -ssl3 -connect $H0:$BRICK_PORT

# Test TLSv1 protocol fails
EXPECT "N" openssl_connect -tls1 -connect $H0:$BRICK_PORT

# Test a HIGH CBC cipher
EXPECT "Y" openssl_connect -cipher AES256-SHA -connect $H0:$BRICK_PORT

# Test EECDH
EXPECT "Y" openssl_connect -cipher EECDH -connect $H0:$BRICK_PORT

# test MD5 fails
EXPECT "N" openssl_connect -cipher DES-CBC3-MD5 -connect $H0:$BRICK_PORT

# test RC4 fails
EXPECT "N" openssl_connect -cipher RC4-SHA -connect $H0:$BRICK_PORT

# test eNULL fails
EXPECT "N" openssl_connect -cipher NULL-SHA256 -connect $H0:$BRICK_PORT

# test SHA2
EXPECT "Y" openssl_connect -cipher AES256-SHA256 -connect $H0:$BRICK_PORT

# test GCM
EXPECT "Y" openssl_connect -cipher AES256-GCM-SHA384 -connect $H0:$BRICK_PORT

# Test DH fails without DH params
EXPECT "N" openssl_connect -cipher EDH -connect $H0:$BRICK_PORT

# Test DH with DH params
TEST $CLI volume set $V0 ssl.dh-param `pwd`/`dirname $0`/dh1024.pem
EXPECT "`pwd`/`dirname $0`/dh1024.pem" volume_option $V0 ssl.dh-param
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count
BRICK_PORT=`brick_port $V0`
EXPECT "Y" openssl_connect -cipher EDH -connect $H0:$BRICK_PORT

# Test the cipher-list option
TEST $CLI volume set $V0 ssl.cipher-list AES256-SHA
EXPECT AES256-SHA volume_option $V0 ssl.cipher-list
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count
BRICK_PORT=`brick_port $V0`
EXPECT "Y" openssl_connect -cipher AES256-SHA -connect $H0:$BRICK_PORT
EXPECT "N" openssl_connect -cipher AES128-SHA -connect $H0:$BRICK_PORT

# Test the ec-curve option
TEST $CLI volume set $V0 ssl.cipher-list EECDH:EDH:!TLSv1
EXPECT EECDH:EDH:!TLSv1 volume_option $V0 ssl.cipher-list
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count
BRICK_PORT=`brick_port $V0`
EXPECT "N" openssl_connect -cipher AES256-SHA -connect $H0:$BRICK_PORT
EXPECT "Y" openssl_connect -cipher EECDH -connect $H0:$BRICK_PORT

TEST $CLI volume set $V0 ssl.ec-curve invalid
EXPECT invalid volume_option $V0 ssl.ec-curve
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count
BRICK_PORT=`brick_port $V0`
EXPECT "N" openssl_connect -cipher EECDH -connect $H0:$BRICK_PORT

TEST $CLI volume set $V0 ssl.ec-curve secp521r1
EXPECT secp521r1 volume_option $V0 ssl.ec-curve
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" online_brick_count
BRICK_PORT=`brick_port $V0`
EXPECT "Y" openssl_connect -cipher EECDH -connect $H0:$BRICK_PORT

# test revocation
# no need to restart the volume since the options are used
# by the client here.
TEST $CLI volume set $V0 ssl.crl-path $TMPDIR
EXPECT $TMPDIR volume_option $V0 ssl.crl-path
$GFS --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT "Y" wait_mount $M0
TEST_FILE=`mktemp $M0/${0##*/}.XXXXXX`
TEST test -f $TEST_FILE
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST openssl ca -batch -config $SSL_CFG -revoke $SSL_CERT 2>&1
TEST openssl ca -config $SSL_CFG -gencrl -out $SSL_CRL 2>&1

# Failed once revoked
$GFS --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT "N" wait_mount $M0
TEST ! test -f $TEST_FILE
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Succeed with CRL disabled
TEST $CLI volume set $V0 ssl.crl-path NULL
EXPECT NULL volume_option $V0 ssl.crl-path
$GFS --volfile-id=$V0 --volfile-server=$H0 $M0
EXPECT "Y" wait_mount $M0
TEST test -f $TEST_FILE

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

rm -rf $TMPDIR
cleanup;
