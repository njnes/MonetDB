/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2016 MonetDB B.V.
 */

/*
 * @f opt_prelude
 * @a M. Kersten
 * These definitions are handy to have around in the optimizer
 */
#include "monetdb_config.h"
#include "opt_prelude.h"
#include "optimizer_private.h"

str abortRef;
str affectedRowsRef;
str aggrRef;
str alarmRef;
str algebraRef;
str batalgebraRef;
str appendidxRef;
str appendRef;
str assertRef;
str attachRef;
str avgRef;
str arrayRef;
str basketRef;
str batcalcRef;
str batRef;
str batstrRef;
str batmtimeRef;
str batmmathRef;
str batxmlRef;
str batsqlRef;
str blockRef;
str bbpRef;
str tidRef;
str dateRef;
str deltaRef;
str subdeltaRef;
str projectdeltaRef;
str binddbatRef;
str bindidxRef;
str bindRef;
str bpmRef;
str bstreamRef;
str calcRef;
str catalogRef;
str clear_tableRef;
str closeRef;
str columnRef;
str columnBindRef;
str commitRef;
str connectRef;
str constraintsRef;
str countRef;
str subcountRef;
str copyRef;
str copy_fromRef;
str export_tableRef;
str count_no_nilRef;
str crossRef;
str createRef;
str dataflowRef;
str datacyclotronRef;
str dblRef;
str defineRef;
str deleteRef;
str depositRef;
str subdiffRef;
str diffRef;
str subinterRef;
str mergecandRef;
str mergepackRef;
str intersectcandRef;
str eqRef;
str disconnectRef;
str evalRef;
str execRef;
str expandRef;
str exportOperationRef;
str finishRef;
str firstnRef;
str getRef;
str generatorRef;
str grabRef;
str groupRef;
str subgroupRef;
str subgroupdoneRef;
str groupbyRef;
str hgeRef;
str hashRef;
str identityRef;
str ifthenelseRef;
str inplaceRef;
str intRef;
str ioRef;
str iteratorRef;
str jsonRef;
str subjoinRef;
str subleftjoinRef;
str subantijoinRef;
str subbandjoinRef;
str subrangejoinRef;
str subthetajoinRef;
str languageRef;
str projectionRef;
str projectionpathRef;
str likesubselectRef;
str likethetasubselectRef;
str ilikesubselectRef;
str ilikethetasubselectRef;
str likeRef;
str ilikeRef;
str not_likeRef;
str not_ilikeRef;
str listRef;
str lockRef;
str lookupRef;
str malRef;
str batmalRef;
str mapiRef;
str markRef;
str mtimeRef;
str multicolumnRef;
str matRef;
str max_no_nilRef;
str maxRef;
str submaxRef;
str submedianRef;
str mdbRef;
str min_no_nilRef;
str minRef;
str subminRef;
str mirrorRef;
str mitosisRef;
str mkeyRef;
str mmathRef;
str multiplexRef;
str manifoldRef;
str mvcRef;
str newRef;
str notRef;
str nextRef;
str oidRef;
str openRef;
str optimizerRef;
str parametersRef;
str packRef;
str pack2Ref;
str passRef;
str partitionRef;
str pcreRef;
str pinRef;
str singleRef;
str plusRef;
str minusRef;
str mulRef;
str divRef;
str printRef;
str preludeRef;
str prodRef;
str subprodRef;
str postludeRef;
str profilerRef;
str projectRef;
str putRef;
str querylogRef;
str queryRef;
str rapiRef;
str batrapiRef;
str pyapiRef;
str batpyapiRef;
str pyapimapRef;
str subeval_aggrRef;
str rankRef;
str dense_rankRef;
str reconnectRef;
str recycleRef;
str refineRef;
str registerRef;
str remapRef;
str remoteRef;
str replaceRef;
str replicatorRef;
str resultSetRef;
str reuseRef;
str row_numberRef;
str rpcRef;
str rsColumnRef;
str schedulerRef;
str selectNotNilRef;
str seriesRef;
str semaRef;
str setAccessRef;
str setWriteModeRef;
str sinkRef;
str sliceRef;
str subsliceRef;
str sortRef;
str sortReverseRef;
str sqlRef;
str srvpoolRef;
str streamsRef;
str startRef;
str starttraceRef;
str stopRef;
str stoptraceRef;
str strRef;
str sumRef;
str subsumRef;
str subavgRef;
str subsortRef;
str takeRef;
str not_uniqueRef;
str sampleRef;
str subuniqueRef;
str subuniformRef;
str unlockRef;
str unpackRef;
str unpinRef;
str updateRef;
str subselectRef;
str timestampRef;
str thetasubselectRef;
str likesubselectRef;
str ilikesubselectRef;
str userRef;
str vectorRef;
str zero_or_oneRef;

void optimizerInit(void)
{
#ifndef HAVE_EMBEDDED
	assert(batRef == NULL);
#endif
	abortRef = putName("abort");
	affectedRowsRef = putName("affectedRows");
	aggrRef = putName("aggr");
	alarmRef = putName("alarm");
	algebraRef = putName("algebra");
	batalgebraRef = putName("batalgebra");
	appendidxRef = putName("append_idxbat");
	appendRef = putName("append");
	assertRef = putName("assert");
	attachRef = putName("attach");
	avgRef = putName("avg");
	arrayRef = putName("array");
	batcalcRef = putName("batcalc");
	basketRef = putName("basket");
	batstrRef = putName("batstr");
	batmtimeRef = putName("batmtime");
	batmmathRef = putName("batmmath");
	batxmlRef = putName("batxml");
	batsqlRef = putName("batsql");
	blockRef = putName("block");
	bbpRef = putName("bbp");
	tidRef = putName("tid");
	deltaRef = putName("delta");
	subdeltaRef = putName("subdelta");
	projectdeltaRef = putName("projectdelta");
	binddbatRef = putName("bind_dbat");
	bindidxRef = putName("bind_idxbat");
	bindRef = putName("bind");
	bpmRef = putName("bpm");
	bstreamRef = putName("bstream");
	calcRef = putName("calc");
	catalogRef = putName("catalog");
	clear_tableRef = putName("clear_table");
	closeRef = putName("close");
	columnRef = putName("column");
	columnBindRef = putName("columnBind");
	commitRef = putName("commit");
	connectRef = putName("connect");
	constraintsRef = putName("constraints");
	countRef = putName("count");
	subcountRef = putName("subcount");
	copyRef = putName("copy");
	copy_fromRef = putName("copy_from");
	export_tableRef = putName("export_table");
	count_no_nilRef = putName("count_no_nil");
	crossRef = putName("crossproduct");
	createRef = putName("create");
	dateRef = putName("date");
	dataflowRef = putName("dataflow");
	datacyclotronRef = putName("datacyclotron");
	dblRef = putName("dbl");
	defineRef = putName("define");
	deleteRef = putName("delete");
	depositRef = putName("deposit");
	subdiffRef = putName("subdiff");
	diffRef = putName("diff");
	subinterRef = putName("subinter");
	mergecandRef= putName("mergecand");
	mergepackRef= putName("mergepack");
	intersectcandRef= putName("intersectcand");
	eqRef = putName("==");
	disconnectRef= putName("disconnect");
	evalRef = putName("eval");
	execRef = putName("exec");
	expandRef = putName("expand");
	exportOperationRef = putName("exportOperation");
	finishRef = putName("finish");
	firstnRef = putName("firstn");
	getRef = putName("get");
	generatorRef = putName("generator");
	grabRef = putName("grab");
	groupRef = putName("group");
	subgroupRef = putName("subgroup");
	subgroupdoneRef= putName("subgroupdone");
	groupbyRef = putName("groupby");
	hgeRef = putName("hge");
	hashRef = putName("hash");
	identityRef = putName("identity");
	ifthenelseRef = putName("ifthenelse");
	inplaceRef = putName("inplace");
	intRef = putName("int");
	ioRef = putName("io");
	iteratorRef = putName("iterator");
	projectionpathRef = putName("projectionpath");
	subjoinRef = putName("subjoin");
	subleftjoinRef = putName("subleftjoin");
	subantijoinRef = putName("subantijoin");
	subbandjoinRef = putName("subbandjoin");
	subrangejoinRef = putName("subrangejoin");
	subthetajoinRef = putName("subthetajoin");
	jsonRef = putName("json");
	languageRef= putName("language");
	projectionRef = putName("projection");
	likesubselectRef = putName("likesubselect");
	ilikesubselectRef = putName("ilikesubselect");
	listRef = putName("list");
	likeRef = putName("like");
	ilikeRef = putName("ilike");
	not_likeRef = putName("not_like");
	not_ilikeRef = putName("not_ilike");
	lockRef = putName("lock");
	lookupRef = putName("lookup");
	malRef = putName("mal");
	batmalRef = putName("batmal");
	mapiRef = putName("mapi");
	markRef = putName("mark");
	mtimeRef = putName("mtime");
	multicolumnRef = putName("multicolumn");
	matRef = putName("mat");
	max_no_nilRef = putName("max_no_nil");
	maxRef = putName("max");
	submaxRef = putName("submax");
	submedianRef = putName("submedian");
	mdbRef = putName("mdb");
	min_no_nilRef = putName("min_no_nil");
	minRef = putName("min");
	subminRef = putName("submin");
	mirrorRef = putName("mirror");
	mitosisRef = putName("mitosis");
	mkeyRef = putName("mkey");
	mmathRef = putName("mmath");
	multiplexRef = putName("multiplex");
	manifoldRef = putName("manifold");
	mvcRef = putName("mvc");
	newRef = putName("new");
	notRef = putName("not");
	nextRef = putName("next");
	oidRef = putName("oid");
	optimizerRef = putName("optimizer");
	openRef = putName("open");
	parametersRef = putName("parameters");
	packRef = putName("pack");
	pack2Ref = putName("pack2");
	passRef = putName("pass");
	partitionRef = putName("partition");
	pcreRef = putName("pcre");
	pinRef = putName("pin");
	plusRef = putName("+");
	minusRef = putName("-");
	mulRef = putName("*");
	divRef = putName("/");
	printRef = putName("print");
	preludeRef = putName("prelude");
	prodRef = putName("prod");
	subprodRef = putName("subprod");
	profilerRef = putName("profiler");
	postludeRef = putName("postlude");
	projectRef = putName("project");
	putRef = putName("put");
	querylogRef = putName("querylog");
	queryRef = putName("query");
	rapiRef = putName("rapi");
	batrapiRef = putName("batrapi");
    pyapiRef = putName("pyapi");
    batpyapiRef = putName("batpyapi");
    pyapimapRef = putName("batpyapimap");
	subeval_aggrRef = putName("subeval_aggr");
	rankRef = putName("rank");
	dense_rankRef = putName("dense_rank");
	reconnectRef = putName("reconnect");
	recycleRef = putName("recycle");
	refineRef = putName("refine");
	registerRef = putName("register");
	remapRef = putName("remap");
	remoteRef = putName("remote");
	replaceRef = putName("replace");
	replicatorRef = putName("replicator");
	resultSetRef = putName("resultSet");
	reuseRef = putName("reuse");
	row_numberRef = putName("row_number");
	rpcRef = putName("rpc");
	rsColumnRef = putName("rsColumn");
	schedulerRef = putName("scheduler");
	selectNotNilRef = putName("selectNotNil");
	seriesRef = putName("series");
	semaRef = putName("sema");
	setAccessRef = putName("setAccess");
	setWriteModeRef= putName("setWriteMode");
	sinkRef = putName("sink");
	sliceRef = putName("slice");
	subsliceRef = putName("subslice");
	singleRef = putName("single");
	sortRef = putName("sort");
	sortReverseRef = putName("sortReverse");
	sqlRef = putName("sql");
	srvpoolRef = putName("srvpool");
	streamsRef = putName("streams");
	startRef = putName("start");
	starttraceRef = putName("starttrace");
	stopRef = putName("stop");
	stoptraceRef = putName("stoptrace");
	strRef = putName("str");
	sumRef = putName("sum");
	subsumRef = putName("subsum");
	subavgRef = putName("subavg");
	subsortRef = putName("subsort");
	takeRef= putName("take");
	timestampRef = putName("timestamp");
	not_uniqueRef= putName("not_unique");
	sampleRef= putName("sample");
	subuniqueRef= putName("subunique");
	subuniformRef= putName("subuniform");
	unlockRef= putName("unlock");
	unpackRef = putName("unpack");
	unpinRef = putName("unpin");
	updateRef = putName("update");
	subselectRef = putName("subselect");
	thetasubselectRef = putName("thetasubselect");
	likesubselectRef = putName("likesubselect");
	likethetasubselectRef = putName("likethetasubselect");
	ilikesubselectRef = putName("ilikesubselect");
	ilikethetasubselectRef = putName("ilikethetasubselect");
	vectorRef = putName("vector");
	zero_or_oneRef = putName("zero_or_one");
	userRef = putName("user");

	/*
	 * Set the optimizer debugging flag
	 */
	{
		int ret;
		str ref= GDKgetenv("opt_debug");
		if ( ref)
			OPTsetDebugStr(&ret,&ref);
	}

	batRef = putName("bat");
}
