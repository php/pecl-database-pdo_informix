--TEST--
pdo_informix: Test error conditions through non-existent tables
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
			$sql = "CREATE TABLE testError(" .
	   			    "id INTEGER," .
	       			"data VARCHAR(50)," .
	       			"attachment VARCHAR(50)," .
	       			"about VARCHAR(50))";

			try {
				$stmt = $this->db->prepare($sql);
				$stmt->execute();
			} catch (PDOException $pe) {
				echo $pe->getMessage();
			}

			$this->db->exec("DROP TABLE testError");
			$sql = "SELECT id FROM FINAL TABLE(INSERT INTO testError(data,about,attachment)values(?,?,?))";

			try {
				$stmt = $this->db->prepare($sql);
				$stmt->execute();
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
