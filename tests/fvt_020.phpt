--TEST--
pdo_informix: rollback
--SKIPIF--
<?php require_once('skipif.inc'); ?>
--FILE--
<?php
    require_once('fvt.inc');
    class Test extends FVTTest {
        public function runTest(){
            $this->connect();
            $this->prepareDB();
            $stmt = $this->db->query( "SELECT count(*) FROM animals" );
            $res = $stmt->fetch( PDO_FETCH_NUM );
            $rows = $res[0];
            echo $rows."\n";

            $this->db->setAttribute( PDO_ATTR_AUTOCOMMIT , false );
            $ac = $this->db->getAttribute( PDO_ATTR_AUTOCOMMIT );
            if( $ac != false ) {
                echo "Cannot set DB2_AUTOCOMMIT_OFF\nCannot run test\n";
                exit;
            }

            $this->db->exec( "DELETE FROM animals" );

            $stmt = $this->db->query( "SELECT count(*) FROM animals" );
            $res = $stmt->fetch( PDO_FETCH_NUM );
            $rows = $res[0];
            echo $rows."\n";

            $this->db->rollBack();

            $stmt = $this->db->query( "SELECT count(*) FROM animals" );
            $res = $stmt->fetch( PDO_FETCH_NUM );
            $rows = $res[0];
            echo $rows."\n";
        }
    }

    $testcase = new Test();
    $testcase->runTest();
?>
--EXPECTF--
7

Fatal error: Uncaught exception 'PDOException' with message 'The auto-commit mode cannot be changed for this driver' in %s/fvt_020.php:12
Stack trace:
#0 %s/fvt_020.php(12): PDO->setAttribute(0, false)
#1 %s/fvt_020.php(36): Test->runTest()
#2 {main}
  thrown in %s/fvt_020.php on line 12
