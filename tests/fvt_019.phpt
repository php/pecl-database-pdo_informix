--TEST--
pdo_informix: Get the last inserted serial numbers
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
				$drop   = 'DROP TABLE testSerial';
				$result = $this->db->exec( $drop );
			} catch( Exception $e ){}

			/* Create the test table */
			$create = 'CREATE TABLE testSerial (id serial, data varchar(50))';
			$result = $this->db->exec( $create );

			/* Insert and get the first serial */
			$insert = 'INSERT INTO testSerial(id, data) values(?, ?);';
			$stmt = $this->db->prepare($insert);
			$data = array('0', 'test1');
			$result = $stmt->execute($data);

			if ($result == true) {
				echo ("Last Serial Inserted: " . $this->db->lastInsertId() . "\n");
			}

			/* Insert the second serial */
			$insert = 'INSERT INTO testSerial(id, data) values(?, ?);';
			$stmt = $this->db->prepare($insert);
			$data = array('0', 'test2');
			$result = $stmt->execute($data);

			/* Insert and get the third serial */
			$insert = 'INSERT INTO testSerial(id, data) values(?, ?);';
			$stmt = $this->db->prepare($insert);
			$data = array('0', 'test3');
			$result = $stmt->execute($data);

			if ($result == true) {
				echo ("Last Serial Inserted: " . $this->db->lastInsertId() . "\n");
			}

			/* Insert and get the forth serial */
			$insert = 'INSERT INTO testSerial(id, data) values(?, ?);';
			$stmt = $this->db->prepare($insert);
			$data = array('0', 'test4');
			$result = $stmt->execute($data);

			if ($result == true) {
				echo ("Last Serial Inserted: " . $this->db->lastInsertId());
			}
		}
	}

	$testcase = new Test();
	$testcase->runTest();
?>
--EXPECT--
Last Serial Inserted: 1
Last Serial Inserted: 3
Last Serial Inserted: 4
