package nl.cwi.monetdb.jdbc;

import java.sql.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

/**
 * A Statement suitable for the Monet database
 * <br /><br />
 * The object used for executing a static SQL statement and returning the
 * results it produces.<br />
 * <br /><br />
 * By default, only one ResultSet object per Statement object can be open at the
 * same time. Therefore, if the reading of one ResultSet object is interleaved
 * with the reading of another, each must have been generated by different
 * Statement objects. All execution methods in the Statement interface implicitly
 * close a Statement's current ResultSet object if an open one exists.
 * <br /><br />
 * The current state of this Statement is that it only implements the
 * executeQuery() which returns a ResultSet where from results can be read
 * and executeUpdate() which doesn't return the affected rows. Commit and
 * rollback are implemented, as is the autoCommit mechanism which relies on
 * server side auto commit.
 *
 * @author Fabian Groffen <Fabian.Groffen@cwi.nl>
 * @version 0.3 (beta release)
 */
public class MonetStatement implements Statement {
	/** the default number of rows that are (attempted to) read at once */
	static final int DEF_FETCHSIZE = 250;
	/** the default value of maxRows, 0 indicates unlimited */
	static final int DEF_MAXROWS = 0;

	/** the headers are stored in this map, a notify is issued on it once new
	 *  data is filled in it, during the fill time it should be empty */
	Map headers = new HashMap();
	/** the number of tuples a result from the server has */
	int tupleCount = -1;
	/** the id as given by the server needed to close it */
	String resultID = null;
	/** the Thread that reads from the server in the background */
	final CacheThread cache;

	/** A socket connection to Mserver */
	private MonetSocket monet;
	/** The parental Connection object */
	private Connection connection;
	/** The last ResultSet object this Statement produced */
	private MonetResultSet lastResultSet;
	/** The warnings this Statement object generated */
	private SQLWarning warnings;
	/** Whether this Statement object is closed or not */
	private boolean closed;
	/** The size of the blocks of results to ask for at the server */
	private int fetchSize = DEF_FETCHSIZE;
	/** The maximum number of rows to return in a ResultSet */
	private int maxRows = DEF_MAXROWS;
	/** The suggested direction of fetching data (not really used) */
	private int fetchDirection = ResultSet.FETCH_FORWARD;
	/** The type of ResultSet to produce; i.e. forward only, random access */
	private int resultSetType = ResultSet.TYPE_FORWARD_ONLY;
	/** The concurrency of the ResultSet to produce */
	private int resultSetConcurrency = ResultSet.CONCUR_READ_ONLY;

	/**
	 * MonetStatement constructor which checks the arguments for validity, tries
	 * to set up a socket to Monet and attempts to login.
	 * This constructor is only accessible to classes from the jdbc package.
	 *
	 * @param monet the connection to Mserver to use
	 * @param connection the connection that created this Statement
	 * @param resultSetType type of ResultSet to produce
	 * @param resultSetConcurrency concurrency of ResultSet to produce
	 * @throws SQLException if an error occurs during login
	 * @throws IllegalArgumentException is one of the arguments is null or empty
	 */
	MonetStatement(
		MonetSocket monet,
		Connection connection,
		int resultSetType,
		int resultSetConcurrency)
		throws SQLException, IllegalArgumentException
	{
		if (connection == null) throw
			new IllegalArgumentException("No Connection given!");
		if (monet == null) throw
			new IllegalArgumentException("No connection with the server given!");

		this.monet = monet;
		this.connection = connection;
		this.resultSetType = resultSetType;
		this.resultSetConcurrency = resultSetConcurrency;

		// check our limits, and generate warnings as appropriate
		if (resultSetConcurrency != ResultSet.CONCUR_READ_ONLY) {
			addWarning("No concurrency mode other then read only is supported, continuing with concurrency level READ_ONLY");
			resultSetConcurrency = ResultSet.CONCUR_READ_ONLY;
		}

		// check type for supported mode
		if (resultSetType == ResultSet.TYPE_SCROLL_SENSITIVE) {
			addWarning("Change sensitive scrolling ResultSet objects are not supported, continuing with a change non-sensitive scrollable cursor.");
			resultSetType = ResultSet.TYPE_SCROLL_INSENSITIVE;
		}

		if (resultSetType == ResultSet.TYPE_FORWARD_ONLY) {
			setFetchSize(DEF_FETCHSIZE);
		} else {
			// use smaller blocks when doing scrollable resultsets
			setFetchSize(DEF_FETCHSIZE / 10);
		}

		cache = new CacheThread();
		// make the thread a little more important
		cache.setPriority(cache.getPriority() + 1);
		// quit the VM if it's waiting for this thread to end
		cache.setDaemon(true);
		cache.start();

		closed = false;
	}

	//== methods of interface Statement

	public void addBatch(String sql) {}
	public void cancel() {}
	public void clearBatch() {}

	/**
	 * Clears all warnings reported for this Statement object. After a call to
	 * this method, the method getWarnings returns null until a new warning is
	 * reported for this Statement object.
	 */
	public void clearWarnings() {
		warnings = null;
	}

	/**
	 * Releases this Statement object's database and JDBC resources immediately
	 * instead of waiting for this to happen when it is automatically closed. It
	 * is generally good practice to release resources as soon as you are
	 * finished with them to avoid tying up database resources.
	 * <br /><br />
	 * Calling the method close on a Statement object that is already closed has
	 * no effect.
	 * <br /><br />
	 * A Statement object is automatically closed when it is garbage collected.
	 * When a Statement object is closed, its current ResultSet object, if one
	 * exists, is also closed.
	 */
	public void close() {
		// close previous ResultSet, if not closed already
		if (lastResultSet != null) lastResultSet.close();
		cache.shutdown();
		closed = true;
	}

	// Chapter 13.1.2.3 of Sun's JDBC 3.0 Specification
	/**
	 * Executes the given SQL statement, which may return multiple results. In
	 * some (uncommon) situations, a single SQL statement may return multiple
	 * result sets and/or update counts. Normally you can ignore this unless
	 * you are (1) executing a stored procedure that you know may return
	 * multiple results or (2) you are dynamically executing an unknown SQL
	 * string.
	 * <br /><br />
	 * The execute method executes an SQL statement and indicates the form of
	 * the first result. You must then use the methods getResultSet or
	 * getUpdateCount to retrieve the result, and getMoreResults to move to any
	 * subsequent result(s).
	 *
	 * @param sql any SQL statement
	 * @return true if the first result is a ResultSet object; false if it is an
	 *         update count or there are no results
	 * @throws SQLException if a database access error occurs
	 */
	public boolean execute(String sql) throws SQLException {
		// close previous ResultSet, if not closed already
		if (lastResultSet != null) {
			lastResultSet.close();
			lastResultSet = null;
		}
		try {
			lastResultSet = new MonetResultSet(
									this,
									sql,
									resultSetType,
									resultSetConcurrency);
		} catch (IllegalArgumentException e) {
			throw new SQLException(e.toString());
		} catch (IOException e) {
			throw new SQLException("IO Exception: " + e.getMessage());
		}
		// don't catch SQLException, it is declared to be thrown

		// find out if we had results
		if (headers.get("emptyheader") != null) {
			// resultless
			lastResultSet.close();
			lastResultSet = null;
			return(false);
		} else {
			// there is a result
			return(true);
		}
	}

	public boolean execute(String sql, int autoGeneratedKeys) {return(false);}
	public boolean execute(String sql, int[] columnIndexed) {return(false);}
	public boolean execute(String sql, String[] columnNames) {return(false);}
	public int[] executeBatch() {return(null);}

	/**
	 * Executes the given SQL statement, which returns a single ResultSet
	 * object.
	 *
	 * @param sql an SQL statement to be sent to the database, typically a
	 *        static SQL SELECT statement
	 * @return a ResultSet object that contains the data produced by the given
	 *         query; never null
	 * @throws SQLException if a database access error occurs or the given SQL
	 *         statement produces anything other than a single ResultSet object
	 */
	public ResultSet executeQuery(String sql) throws SQLException {
		if (execute(sql) != true)
			throw new SQLException("Query did not produce a result set");

		return(getResultSet());
	}

	/**
	 * Executes the given SQL statement, which may be an INSERT, UPDATE, or
	 * DELETE statement or an SQL statement that returns nothing, such as an
	 * SQL DDL statement.
	 * <br /><br />
	 * make an implementation which returns affected rows, need protocol
	 * modification for that!!!
	 *
	 * @param sql an SQL INSERT, UPDATE or DELETE statement or an SQL statement
	 *        that returns nothing
	 * @return either the row count for INSERT, UPDATE  or DELETE statements, or
	 *         0 for SQL statements that return nothing<br />
	 *         <b>currently always returns -1 since the mapi protocol doesn't
	 *         return the affected rows!!!</b>
	 * @throws SQLException if a database access error occurs or the given SQL
	 *         statement produces a ResultSet object
	 */
	public int executeUpdate(String sql) throws SQLException {
		if (execute(sql) != false)
			throw new SQLException("Query produced a result set");

		return(getUpdateCount());
	}

	public int executeUpdate(String sql, int autoGeneratedKeys) {return(-1);}
	public int executeUpdate(String sql, int[] columnIndexed) {return(-1);}
	public int executeUpdate(String sql, String[] columnNames) {return(-1);}

	/**
	 * Retrieves the Connection object that produced this Statement object.
	 *
	 * @return the connection that produced this statement
	 */
	public Connection getConnection() {
		return(connection);
	}

	/**
	 * Retrieves the direction for fetching rows from database tables that is
	 * the default for result sets generated from this Statement object. If
	 * this Statement object has not set a fetch direction by calling the
	 * method setFetchDirection, the return value is ResultSet.FETCH_FORWARD.
	 *
	 * @return the default fetch direction for result sets generated from this
	 *         Statement object
	 */
	public int getFetchDirection() {
		return(fetchDirection);
	}

	/**
	 * Retrieves the number of result set rows that is the default fetch size
	 * for ResultSet objects generated from this Statement object. If this
	 * Statement object has not set a fetch size by calling the method
	 * setFetchSize, the return value is DEF_FETCHSIZE.
	 *
	 * @return the default fetch size for result sets generated from this
	 *         Statement object
	 */
	public int getFetchSize() {
		return(fetchSize);
	}

	public ResultSet getGeneratedKeys() {return(null);}
	public int getMaxFieldSize() {return(-1);}

	/**
	 * Retrieves the maximum number of rows that a ResultSet object produced by
	 * this Statement object can contain. If this limit is exceeded, the excess
	 * rows are silently dropped.
	 *
	 * @return the current maximum number of rows for a ResultSet object
	 *         produced by this Statement object; zero means there is no limit
	 */
	public int getMaxRows() {
		return(maxRows);
	}

	public boolean getMoreResults() {return(false);}
	public boolean getMoreResults(int current) {return(false);}
	public int getQueryTimeout() {return(-1);}

	/**
	 * Retrieves the current result as a ResultSet object. This method should
	 * be called only once per result.
	 *
	 * @return the current result as a ResultSet object or null if the result
	 *         is an update count or there are no more results
	 */
	public ResultSet getResultSet() {
		return(lastResultSet);
	}

	/**
	 * Retrieves the result set concurrency for ResultSet objects generated
	 * by this Statement object.
	 *
	 * @return either ResultSet.CONCUR_READ_ONLY or ResultSet.CONCUR_UPDATABLE
	 */
	public int getResultSetConcurrency() {
		return(resultSetConcurrency);
	}

	public int getResultSetHoldability() {return(-1);}

	/**
	 * Retrieves the result set type for ResultSet objects generated by this
	 * Statement object.
	 *
	 * @return one of ResultSet.TYPE_FORWARD_ONLY,
	 *         ResultSet.TYPE_SCROLL_INSENSITIVE, or
	 *         ResultSet.TYPE_SCROLL_SENSITIVE
	 */
	public int getResultSetType() {
		return(resultSetType);
	}

	/**
	 * Retrieves the current result as an update count; if the result is a
	 * ResultSet object or there are no more results, -1 is returned. This
	 * method should be called only once per result.
	 *
	 * @return the current result as an update count; -1 if the current result
	 *         is a ResultSet object or there are no more results
	 */
	public int getUpdateCount() {
		// there is currently no way to get the update count, so this is fixed
		// on -1 for now :(
		return(-1);
	}

	/**
	 * Retrieves the first warning reported by calls on this Statement object.
	 * If there is more than one warning, subsequent warnings will be chained to
	 * the first one and can be retrieved by calling the method
	 * SQLWarning.getNextWarning on the warning that was retrieved previously.
	 * <br /><br />
	 * This method may not be called on a closed statement; doing so will cause
	 * an SQLException to be thrown.
	 * <br /><br />
	 * Note: Subsequent warnings will be chained to this SQLWarning.
	 *
	 * @return the first SQLWarning object or null if there are none
	 * @throws SQLException if a database access error occurs or this method is
	 *         called on a closed connection
	 */
	public SQLWarning getWarnings() throws SQLException {
		if (closed) throw new SQLException("Cannot call on closed Statement");

		// if there are no warnings, this will be null, which fits with the
		// specification.
		return(warnings);
	}

	public void setCursorName(String name) {}
	public void setEscapeProcessing(boolean enable) {}

	/**
	 * Gives the driver a hint as to the direction in which rows will be
	 * processed in ResultSet objects created using this Statement object.
	 * The default value is ResultSet.FETCH_FORWARD.
	 * <br /><br />
	 * Note that this method sets the default fetch direction for result sets
	 * generated by this Statement object. Each result set has its own methods
	 * for getting and setting its own fetch direction.
	 *
	 * @param direction the initial direction for processing rows
	 * @throws SQLException if a database access error occurs or the given
	 *         direction is not one of ResultSet.FETCH_FORWARD,
	 *         ResultSet.FETCH_REVERSE, or ResultSet.FETCH_UNKNOWN
	 */
	public void setFetchDirection(int direction) throws SQLException {
		if (direction == ResultSet.FETCH_FORWARD ||
		    direction == ResultSet.FETCH_REVERSE ||
			direction == ResultSet.FETCH_UNKNOWN)
		{
			fetchDirection = direction;
		} else {
			throw new SQLException("Illegal direction: " + direction);
		}
	}

	/**
	 * Gives the JDBC driver a hint as to the number of rows that should be
	 * fetched from the database when more rows are needed. The number of rows
	 * specified affects only result sets created using this statement. If the
	 * value specified is zero, then the hint is ignored. The default value is
	 * defined in DEF_FETCHSIZE.<br />
	 *
	 * @param rows the number of rows to fetch
	 * @throws SQLException if the condition 0 <= rows <= this.getMaxRows()
	 *         is not satisfied.
	 */
	public void setFetchSize(int rows) throws SQLException {
		if (rows == 0) {
			fetchSize = DEF_FETCHSIZE;
		} else if (rows > 0 && !(getMaxRows() != 0 && rows > getMaxRows())) {
			fetchSize = rows;
		} else {
			throw new SQLException("Illegal fetch size value: " + rows);
		}
	}

	public void setMaxFieldSize(int max) {}

	/**
	 * Sets the limit for the maximum number of rows that any ResultSet object
	 * can contain to the given number. If the limit is exceeded, the excess
	 * rows are silently dropped.
	 *
	 * @param max the new max rows limit; zero means there is no limit
	 * @throws SQLException if the condition max >= 0 is not satisfied
	 */
	public void setMaxRows(int max) throws SQLException {
		if (max < 0) throw new SQLException("Illegal max value: " + max);
		maxRows = max;
	}

	public void setQueryTimeout(int seconds) {}

	//== end methods of interface Statement

	protected void finalize() {
		close();
	}

	/**
	 * Adds a warning to the pile of warnings this Statement object has. If
	 * there were no warnings (or clearWarnings was called) this warning will
	 * be the first, otherwise this warning will get appended to the current
	 * warning.
	 *
	 * @param reason the warning message
	 */
	private void addWarning(String reason) {
		if (warnings == null) {
			warnings = new SQLWarning(reason);
		} else {
			warnings.setNextWarning(new SQLWarning(reason));
		}
	}


	/**
	 * The CacheThread represents a pseudo array holding all results. For real
	 * only a part of the complete result set is cached, but upon request for
	 * a result outside the actual cache, the cache is shuffled so the result
	 * comes available.
	 */
	class CacheThread extends Thread {
		/** object used for internal locking */
		private boolean[] fetch = new boolean[1];
		/** lock object used when some other thread waits for a line */
		int[] fill = {-1};
		/** the last error messages */
		private String error;
		/** a regular expressions that we often use (compile them once) to split
		 *  the headers into an array */
		private final Pattern splitPattern = Pattern.compile(",\t");

		/** The current block being read */
		private int curBlock;
		/** The last line read from the stream, the max available for retrieval
		 *  without having to locking and wait for it */
		private int[] maxLine = new int[2];
		/** The size of the cache */
		private int cacheSize;
		/** The query being executed */
		private String query = null;
		/** The actual cache where results get stored */
		private String line[];

		/** Whether this CacheThread is still supposed to run or not */
		private boolean finished = false;
		/** Indicates that results are already available and do not need to be
		 *  requested using Xexport */
		private boolean onPreFetchStartReading = false;

		/** The headers are stored in this Map as long as it is believed there
		 *  are more to come */
		Map tmpHeaders = new HashMap();

		public void run() {
			synchronized(fetch) {;
				while(!finished) {
					if (fetch[0] == false) {
						try {
							fetch.wait();
						} catch (InterruptedException e) {
							// possible shutdown of this thread?
							// next condition check will act appropriately
						}
					} else {
						try {
							fillCache();
						} catch (SQLException e) {
							// store error, and (attempt to) go ahead
							error = e.getMessage();
							// make sure the headers are notified
							try {
								completeHeaders();
							} catch (SQLException ex) {}
							// make sure a getLine call gets the error
							synchronized(maxLine) {
								maxLine.notify();
							}
						} catch (IOException e) {
							// the connection died on us
							finished = true;
							error = e.toString();
							// make sure the headers are notified
							try {
								completeHeaders();
							} catch (SQLException ex) {}
							// make sure a getLine call gets the error
							synchronized(maxLine) {
								maxLine.notify();
							}
						} finally {
							// make sure we always reset this value
							fetch[0] = false;
						}
					}
				}
			}
		}

		/**
		 * Lets this thread terminate (die) so it turns into a normal object and
		 * can be garbage collected.
		 * A call to this function should be made when the parent Statement
		 * closes since the VM will not close when once or more of these threads
		 * are running.
		 */
		void shutdown() {
			finished = true;
			// if the thread is blocking on a wait, break it out
			synchronized(fetch) {
				fetch.notify();
			}
		}

		/**
		 * Returns whether an error has occurred since the last call to
		 * getError() or not
		 *
		 * @return true if an error has occured
		 * @see #getError()
		 */
		synchronized boolean hasError() {
			return(error == null ? false : true);
		}

		/**
		 * Returns the last error since the last call to this function. On each
		 * call to this method the error message is cleared.
		 *
		 * @return the last error message
		 */
		synchronized String getError() {
			String ret = error;
			error = null;
			return(ret);
		}

		/**
		 * Indicates to this cache thread that a new result can and should be
		 * read from the socket. The headers map is cleared, after which the
		 * socket is read. The map object will be notified when the first row is
		 * read which is not a header.
		 *
		 * @throws SQLException if this thread is not alive
		 */
		synchronized void newResult(String query) throws SQLException {
			synchronized(headers) {
				headers.clear();
				tmpHeaders.clear();
				this.query = query;
				tupleCount = 0;
				resultID = null;
			}
			// clear error message
			if (hasError()) System.out.println(getError());

			onPreFetchStartReading = true;
			preFetch(0);
		}

		private void preFetch(int block) throws SQLException {
			if (!isAlive()) throw new SQLException("Cache Thread is dead!");

			synchronized(fetch) {
				curBlock = block;
				maxLine[0] = 0;
				fetch[0] = true;
				fetch.notify();
			}
		}

		/**
		 * Reads from the Monet server till either the cache is full, the
		 * resultset is empty or an (IO)error occurs.
		 *
		 * @throws SQLException if a Monet error occurs
		 * @throws IOExceptioni if an IO error occurs
		 */
		private void fillCache() throws SQLException, IOException {
			boolean expectHeader = false;
			// make sure we're the only one reading from the socket
			synchronized (monet) {
				if (onPreFetchStartReading) {
					// Don't issue Xexport, but wait for sync and send query

					// make sure we're ready to send query; read data till we have the
					// prompt it is possible (and most likely) that we already have
					// the prompt and do not have to skip any lines. Ignore errors from
					// previous ResultSets.
					monet.waitForPrompt();

					// send the query
					monet.writeln(query);

					onPreFetchStartReading = false;
					expectHeader = true;
				} else {
					if (headers.size() == 0) throw
						new AssertionError("No header information!!!");
					// get new results
					int size = Math.min(cacheSize, tupleCount - (curBlock * cacheSize));
					if (size == 0) throw
						new AssertionError("Should not fetch empty block");
					// ask the server for the next block of results
					monet.writeln("Xexport " + resultID + " " +
						curBlock * cacheSize + " " + size);
					maxLine[0] = 0;
				}

				// go for new results
				String tmpLine, errMsg = "";
				do {
					tmpLine = monet.readLine();
					if (monet.getLineType() == MonetSocket.ERROR) {
						// concatenate error message
						errMsg = errMsg + tmpLine.substring(1) + "\n";
					} else if (monet.getLineType() == MonetSocket.HEADER) {
						if (!expectHeader) {
							throw new SQLException("Unexpected header found");
						} else {
							int pos = tmpLine.lastIndexOf("#");
							if (pos == -1) throw
								new SQLException("Illegal header: " + tmpLine);

							String[] values =
								splitPattern.split(tmpLine.substring(1, pos - 1));
							for (int i = 0; i < values.length; i++) {
								values[i] = values[i].trim();
							}
							tmpHeaders.put(
								tmpLine.substring(pos + 1).trim(),
								values
							);
						}
					} else if (monet.getLineType() == MonetSocket.RESULT) {
						// complete the header info and notify
						if (expectHeader) {
							expectHeader = false;
							completeHeaders();
						}

						synchronized(maxLine) {
							line[maxLine[0]++] = tmpLine;
							if (maxLine[0] >= maxLine[1]) maxLine.notify();
						}
					} else if (monet.getLineType() == MonetSocket.EMPTY) {
						// empty, will mean Monet stopped somehow (crash?)
						throw new IOException("Unexpected end of stream, Mserver still alive?");
					}
				} while (monet.getLineType() != MonetSocket.PROMPT1);
				// if there were errors, send them
				if (errMsg != "") throw new SQLException(errMsg.trim());

				if (expectHeader) {
					expectHeader = false;
					completeHeaders();
				}
				// check for consistency
				if (maxLine[0] != Math.min(cacheSize, tupleCount - (curBlock * cacheSize)))
					throw new SQLException("Aiieeee!!! Inconsistant data! Monet said there were " + tupleCount + " tuples, but it only returned " + maxLine[0]);
			}
		}

		/**
		 * Checks if the headers map is empty, and fills with a specual value
		 * is appropriate. If the map is not empty, it is tried to parse
		 * tupleCount and resultID.
		 * When the required headers are found, the cache is initialized, and
		 * the right cacheSize is set.
		 * The headers object is notified at the end of this method
		 *
		 * @throws SQLException if a required header is missing or unparsable
		 */
		private void completeHeaders() throws SQLException {
			synchronized(headers) {
				if (tmpHeaders.size() != 0) {
					Object tmp = tmpHeaders.get("tuplecount");
					if (tmp == null) throw new SQLException("Required header missing!");
					try {
						tupleCount = Integer.parseInt(((String[])tmp)[0]);
						if (maxRows != 0)
							tupleCount = Math.min(tupleCount, maxRows);
					} catch (NumberFormatException e) {
						throw new SQLException("Illegal header, unparsable int for tuplecount: " + ((String[])headers.get("tuplecount"))[0]);
					}
					tmp = tmpHeaders.get("id");
					if (tmp == null) throw new SQLException("Required header missing!");
					resultID = ((String[])tmp)[0];

					// make headers available for the public :)
					headers.putAll(tmpHeaders);

					// make sure the cache size is minimal to
					// reduce overhead and memory usage
					cacheSize = Math.min(tupleCount, fetchSize);
					// create cache buffer
					line = new String[cacheSize];
				} else {
					// a no header query (sigh, yes that can happen)
					// put in some special value to let the resultset
					// know this is it...
					headers.put("emptyheader", "true");
				}
				headers.notify();
			}
		}

		/**
		 * Returns a line from the cache. If the line is already present in the
		 * cache, it is returned, if not apropriate actions are taken to make
		 * sure the right block is being fetched and as soon as the requested
		 * line is fetched it is returned.
		 *
		 * @param row the row in the result set to return
		 * @return the exact row read as requested or null if the requested row
		 *         is out of the scope of the result set
		 * @throws SQLException if an database error occurs
		 */
		String getLine(int row) throws SQLException {
			if (headers.size() == 0) throw
				new SQLException("Cannot retrieve data before headers are retrieved!");

			if (row >= tupleCount || row < 0) return null;

			int block = row / cacheSize;
			int blockLine = row % cacheSize;


			// do we have the right block loaded?
			if (block == curBlock) {
				// yay
			} else {
				// ok, need to fetch cache block first
				preFetch(block);
			}

			synchronized(maxLine) {
				while (maxLine[0] <= blockLine) {
					// throw exception if we have one
					if (hasError()) throw new SQLException(getError());
					// wait for the cache to be filled
					try {
						maxLine[1] = blockLine;
						maxLine.wait();
					} catch (InterruptedException e) {
						// hmm, someone woke us up! No good!
						throw new SQLException("Timeout expired, got tired of waiting...");
					}
				}
			}
			return(line[blockLine]);
		}
	}
}
