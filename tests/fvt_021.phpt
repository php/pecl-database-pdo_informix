--TEST--
pdo_informix: commit
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
            $this->db->exec( "DELETE FROM animals" );

            $stmt = $this->db->query( "SELECT count(*) FROM animals" );
            $res = $stmt->fetch( PDO_FETCH_NUM );
            $rows = $res[0];
            echo $rows."\n";

            $this->db->commit();

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

Fatal error: Uncaught exception 'PDOException' with message 'The auto-commit mode cannot be changed for this driver' in %s/fvt_021.php:12
Stack trace:
#0 %s/fvt_021.php(12): PDO->setAttribute(0, false)
#1 %s/fvt_021.php(30): Test->runTest()
#2 {main}
  thrown in %s/fvt_021.php on line 12
