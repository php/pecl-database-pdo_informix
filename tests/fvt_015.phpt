--TEST--
pdo_informix: Insert/select text/byte columns with binding to local variable or stream
--SKIPIF--
<?php require_once('skipif.inc'); ?>
--FILE--
<?php
	require_once('fvt.inc');
	class Test extends FVTTest
	{
		public function runTest()
		{
			$this->connect();

			try {
				/* Drop the test table, in case it exists */
				$drop = 'DROP TABLE animals';
				$result = $this->db->exec( $drop );
			} catch( Exception $e ){}

			/* Create the test table */
			$create = 'CREATE TABLE animals (id INTEGER, my_clob text, my_blob byte)';
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
			$fp1 = fopen( dirname(__FILE__) . "/clob.dat" , "rb" );
			$fp2 = fopen( dirname(__FILE__) . "/spook.png" , "rb" );
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
--EXPECTF--
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
    resource(%i) of type (stream)
    [1]=>
    resource(%i) of type (stream)
    ["my_blob"]=>
    resource(%i) of type (stream)
    [2]=>
    resource(%i) of type (stream)
  }
  [1]=>
  array(6) {
    ["id"]=>
    string(1) "1"
    [0]=>
    string(1) "1"
    ["my_clob"]=>
    resource(%i) of type (stream)
    [1]=>
    resource(%i) of type (stream)
    ["my_blob"]=>
    resource(%i) of type (stream)
    [2]=>
    resource(%i) of type (stream)
  }
}
string(1) "0"
resource(%i) of type (stream)
resource(%i) of type (stream)
string(1) "1"
resource(%i) of type (stream)
resource(%i) of type (stream)
done
