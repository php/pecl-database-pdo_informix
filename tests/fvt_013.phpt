--TEST--
pdo_informix: Scrollable cursor; retrieve negative row
--SKIPIF--
<?php require_once('skipif.inc'); ?>
--FILE--
<?php
    require_once('fvt.inc');
    class Test extends FVTTest {
        public function runTest(){
            $this->connect();
            $this->prepareDB();

            $stmt = $this->db->prepare( "SELECT * FROM animals" , array(PDO_ATTR_CURSOR, PDO_CURSOR_SCROLL) );
            $stmt->execute();
            var_dump( $stmt->fetchAll() );
            $stmt->execute();
            try{
                $row = $stmt->fetch( PDO_FETCH_BOTH , PDO_FETCH_ORI_ABS , -1 );
                var_dump( $row );
            }catch( PDOException $e ){
                $info = $stmt->errorInfo();
                if( $info[1] == -11086 )
                    print "Cannot retrieve negative row\n";
                else
                    print $e . "\n";
            }
            $stmt->closeCursor();
        }
    }

    $testcase = new Test();
    $testcase->runTest();
?>
--EXPECT--
array(7) {
  [0]=>
  array(8) {
    ["id"]=>
    string(1) "0"
    [0]=>
    string(1) "0"
    ["breed"]=>
    string(3) "cat"
    [1]=>
    string(3) "cat"
    ["name"]=>
    string(16) "Pook            "
    [2]=>
    string(16) "Pook            "
    ["weight"]=>
    string(4) "3.20"
    [3]=>
    string(4) "3.20"
  }
  [1]=>
  array(8) {
    ["id"]=>
    string(1) "1"
    [0]=>
    string(1) "1"
    ["breed"]=>
    string(3) "dog"
    [1]=>
    string(3) "dog"
    ["name"]=>
    string(16) "Peaches         "
    [2]=>
    string(16) "Peaches         "
    ["weight"]=>
    string(5) "12.30"
    [3]=>
    string(5) "12.30"
  }
  [2]=>
  array(8) {
    ["id"]=>
    string(1) "2"
    [0]=>
    string(1) "2"
    ["breed"]=>
    string(5) "horse"
    [1]=>
    string(5) "horse"
    ["name"]=>
    string(16) "Smarty          "
    [2]=>
    string(16) "Smarty          "
    ["weight"]=>
    string(6) "350.00"
    [3]=>
    string(6) "350.00"
  }
  [3]=>
  array(8) {
    ["id"]=>
    string(1) "3"
    [0]=>
    string(1) "3"
    ["breed"]=>
    string(9) "gold fish"
    [1]=>
    string(9) "gold fish"
    ["name"]=>
    string(16) "Bubbles         "
    [2]=>
    string(16) "Bubbles         "
    ["weight"]=>
    string(4) "0.10"
    [3]=>
    string(4) "0.10"
  }
  [4]=>
  array(8) {
    ["id"]=>
    string(1) "4"
    [0]=>
    string(1) "4"
    ["breed"]=>
    string(10) "budgerigar"
    [1]=>
    string(10) "budgerigar"
    ["name"]=>
    string(16) "Gizmo           "
    [2]=>
    string(16) "Gizmo           "
    ["weight"]=>
    string(4) "0.20"
    [3]=>
    string(4) "0.20"
  }
  [5]=>
  array(8) {
    ["id"]=>
    string(1) "5"
    [0]=>
    string(1) "5"
    ["breed"]=>
    string(4) "goat"
    [1]=>
    string(4) "goat"
    ["name"]=>
    string(16) "Rickety Ride    "
    [2]=>
    string(16) "Rickety Ride    "
    ["weight"]=>
    string(4) "9.70"
    [3]=>
    string(4) "9.70"
  }
  [6]=>
  array(8) {
    ["id"]=>
    string(1) "6"
    [0]=>
    string(1) "6"
    ["breed"]=>
    string(5) "llama"
    [1]=>
    string(5) "llama"
    ["name"]=>
    string(16) "Sweater         "
    [2]=>
    string(16) "Sweater         "
    ["weight"]=>
    string(6) "150.00"
    [3]=>
    string(6) "150.00"
  }
}
Cannot retrieve negative row
