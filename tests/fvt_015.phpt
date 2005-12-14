--TEST--
pdo_informix: Insert/select clob/blob columns with binding to local variable or stream
--SKIPIF--
<?php require_once('skipif.inc'); ?>
--FILE--
<?php
    require_once('fvt.inc');
    class Test extends FVTTest {
        public function runTest(){
            $this->connect();

            try{
                // Drop the test table, in case it exists
                $drop = 'DROP TABLE animals';
                $result = $this->db->exec( $drop );
            }catch( Exception $e ){}

            // Create the test table
            $create = 'CREATE TABLE animals (id INTEGER, my_clob clob, my_blob blob)';
            $result = $this->db->exec( $create );

            $stmt = $this->db->prepare('insert into animals (id,my_clob,my_blob) values (:id,:my_clob,:my_blob)');
            $clob = "test clob data\n";
            $blob = "test blob data\n";
            print "inserting from php variable\n";
            $stmt->bindValue( ':id' , 0 );
            $stmt->bindParam( ':my_clob' , $clob , PDO::PARAM_LOB , strlen($clob) );
            $stmt->bindParam( ':my_blob' , $blob , PDO::PARAM_LOB , strlen($blob) );
            $stmt->execute();
            print "succesful\n";

            print "inserting from php file stream\n";
            $fp1 = fopen( "clob.dat" , "rb" );
            $fp2 = fopen( "spook.png" , "rb" );
            $stmt = $this->db->prepare('insert into animals (id,my_clob,my_blob) values (:id,:my_clob,:my_blob)');
            $stmt->bindValue( ':id' , 1 );
            $stmt->bindParam( ':my_clob' , $fp1 , PDO::PARAM_LOB );
            $stmt->bindParam( ':my_blob' , $fp2 , PDO::PARAM_LOB );
            $stmt->execute();
            print "succesful\n";

            print "runnign query\n";
            $stmt = $this->db->prepare( 'select id,my_clob,my_blob from animals' );
            $rs = $stmt->execute();
            $res = $stmt->fetchAll();
            var_dump( $res );
            
            $stmt->bindColumn( 'id' , $id );
            $stmt->bindColumn( 'my_clob' , $clob , PDO::PARAM_LOB );
            $stmt->bindColumn( 'my_blob' , $blob , PDO::PARAM_LOB );
            $rs = $stmt->execute();
            while ($stmt->fetch(PDO::FETCH_BOUND)) {
                var_dump( $id );
                var_dump( $clob );
                var_dump( $blob );
            }
            print "done\n";
        }
    }

$testcase = new Test();
$testcase->runTest();
?>
--EXPECT--
inserting from php variable
succesful
inserting from php file stream
succesful
runnign query
array(2) {
  [0]=>
  array(6) {
    ["id"]=>
    string(1) "0"
    [0]=>
    string(1) "0"
    ["my_clob"]=>
    resource(8) of type (stream)
    [1]=>
    resource(8) of type (stream)
    ["my_blob"]=>
    resource(9) of type (stream)
    [2]=>
    resource(9) of type (stream)
  }
  [1]=>
  array(6) {
    ["id"]=>
    string(1) "1"
    [0]=>
    string(1) "1"
    ["my_clob"]=>
    resource(10) of type (stream)
    [1]=>
    resource(10) of type (stream)
    ["my_blob"]=>
    resource(11) of type (stream)
    [2]=>
    resource(11) of type (stream)
  }
}
string(1) "0"
resource(12) of type (stream)
resource(13) of type (stream)
string(1) "1"
resource(14) of type (stream)
resource(15) of type (stream)
done
