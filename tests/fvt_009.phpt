--TEST--
pdo_informix: Test error conditions through faulty SQL
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
			$parmno = "200010";
			try {
				$stmt = $this->db->prepare("SELECT empno, lastname, bonus, FROM employee WHERE empno > ?");
				$stmt->execute( array( $parmno ));
				while ($row = $stmt->fetch()) {
					print_r($row);
				}
			}	catch (PDOException $pe) {
				echo "Error code:\n";
				print_r($this->db->errorCode());
				echo "\n";
				echo "Error info:\n";
				print_r($this->db->errorInfo());
			}
		}
	}

	$testcase = new Test();
	$testcase->runTest();
?>
--EXPECTF--
Error code:
42000
Error info:
Array
(
    [0] => 42000
    [1] => -201
    [2] => [Informix][Informix ODBC Driver][Informix]A syntax error has occurred. (SQLPrepare[-201] at %s)
)
